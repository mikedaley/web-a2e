/*
 * condition_evaluator.cpp - Expression parser for debugger breakpoint conditions and watches
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "condition_evaluator.hpp"
#include "../basic/applesoft_vars.hpp"
#include "../emulator.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>

namespace a2e {

// Read a 5-byte Applesoft float out of memory and truncate toward zero.
// The decoding itself lives in ApplesoftVars so the debugger's variable
// inspector and this evaluator cannot drift apart.
static int32_t decodeApplesoftFloat(const MachineView& emu, uint16_t addr) {
  uint8_t bytes[APPLESOFT_FLOAT_SIZE];
  for (int i = 0; i < APPLESOFT_FLOAT_SIZE; i++) {
    bytes[i] = emu.peek(static_cast<uint16_t>(addr + i));
  }
  return static_cast<int32_t>(ApplesoftVars::decodeFloat(bytes));
}

// Read a BASIC simple variable value by its encoded name bytes
// Walks VARTAB to ARYTAB looking for matching name
static int32_t readBasicVariable(const MachineView& emu, uint8_t nameB1, uint8_t nameB2) {
  uint16_t vartab = emu.peek(0x69) | (emu.peek(0x6A) << 8);
  uint16_t arytab = emu.peek(0x6B) | (emu.peek(0x6C) << 8);

  if (vartab == 0 || arytab == 0 || vartab >= arytab) return 0;

  uint16_t addr = vartab;
  while (addr < arytab) {
    uint8_t b1 = emu.peek(addr);
    uint8_t b2 = emu.peek(addr + 1);

    if (b1 == nameB1 && b2 == nameB2) {
      // Found it - determine type from high bits
      bool isInteger = (b1 & 0x80) && (b2 & 0x80);
      if (isInteger) {
        uint8_t high = emu.peek(addr + 2);
        uint8_t low = emu.peek(addr + 3);
        int16_t val = static_cast<int16_t>((high << 8) | low);
        return static_cast<int32_t>(val);
      }
      // Real number (5-byte float)
      return decodeApplesoftFloat(emu, addr + 2);
    }
    addr += 7; // Each simple var is 7 bytes
  }
  return 0; // Variable not found
}

// Read a BASIC array element by encoded name bytes and flat index
static int32_t readBasicArrayElement(const MachineView& emu, uint8_t nameB1, uint8_t nameB2, int32_t flatIndex) {
  uint16_t arytab = emu.peek(0x6B) | (emu.peek(0x6C) << 8);
  uint16_t strend = emu.peek(0x6D) | (emu.peek(0x6E) << 8);

  if (arytab == 0 || strend == 0 || arytab >= strend) return 0;

  uint16_t addr = arytab;
  while (addr < strend) {
    uint8_t b1 = emu.peek(addr);
    uint8_t b2 = emu.peek(addr + 1);
    uint16_t totalSize = emu.peek(addr + 2) | (emu.peek(addr + 3) << 8);

    if (b1 == nameB1 && b2 == nameB2) {
      uint8_t numDims = emu.peek(addr + 4);
      uint16_t dataStart = addr + 5 + numDims * 2;

      bool isInteger = (b1 & 0x80) && (b2 & 0x80);
      int elementSize = isInteger ? 2 : 5;

      uint16_t elemAddr = dataStart + flatIndex * elementSize;
      if (isInteger) {
        uint8_t high = emu.peek(elemAddr);
        uint8_t low = emu.peek(elemAddr + 1);
        int16_t val = static_cast<int16_t>((high << 8) | low);
        return static_cast<int32_t>(val);
      }
      return decodeApplesoftFloat(emu, elemAddr);
    }
    addr += totalSize;
  }
  return 0;
}

// Read a BASIC 2D array element by encoded name bytes and two indices
// Applesoft stores dimensions in reverse order: DIM A(M,N) stores [N+1, M+1]
// Element A(i,j) flat index = i * dim2size + j  (dim2size is the FIRST stored dimension)
static int32_t readBasicArrayElement2D(const MachineView& emu, uint8_t nameB1, uint8_t nameB2, int32_t idx1, int32_t idx2) {
  uint16_t arytab = emu.peek(0x6B) | (emu.peek(0x6C) << 8);
  uint16_t strend = emu.peek(0x6D) | (emu.peek(0x6E) << 8);

  if (arytab == 0 || strend == 0 || arytab >= strend) return 0;

  uint16_t addr = arytab;
  while (addr < strend) {
    uint8_t b1 = emu.peek(addr);
    uint8_t b2 = emu.peek(addr + 1);
    uint16_t totalSize = emu.peek(addr + 2) | (emu.peek(addr + 3) << 8);

    if (b1 == nameB1 && b2 == nameB2) {
      uint8_t numDims = emu.peek(addr + 4);
      if (numDims < 2) return 0;

      // Applesoft stores arrays in column-major order with reversed dimensions.
      // For DIM A(M,N): dims[0]=N+1, dims[1]=M+1
      // Flat index for A(i,j) = j * dims[1] + i
      uint16_t dim1Size = (emu.peek(addr + 7) << 8) | emu.peek(addr + 8);

      int32_t flatIndex = idx2 * dim1Size + idx1;
      uint16_t dataStart = addr + 5 + numDims * 2;

      bool isInteger = (b1 & 0x80) && (b2 & 0x80);
      int elementSize = isInteger ? 2 : 5;

      uint16_t elemAddr = dataStart + flatIndex * elementSize;
      if (isInteger) {
        uint8_t high = emu.peek(elemAddr);
        uint8_t low = emu.peek(elemAddr + 1);
        int16_t val = static_cast<int16_t>((high << 8) | low);
        return static_cast<int32_t>(val);
      }
      return decodeApplesoftFloat(emu, elemAddr);
    }
    addr += totalSize;
  }
  return 0;
}

char ConditionEvaluator::errorBuf_[128] = "";

int ConditionEvaluator::tokenize(const char* expr, Token* tokens, int maxTokens) {
  int count = 0;
  int i = 0;
  int len = static_cast<int>(strlen(expr));

  while (i < len && count < maxTokens - 1) {
    // Skip whitespace
    if (expr[i] == ' ' || expr[i] == '\t') {
      i++;
      continue;
    }

    // Two-char operators
    if (i + 1 < len) {
      char c0 = expr[i], c1 = expr[i + 1];
      if ((c0 == '=' && c1 == '=') || (c0 == '!' && c1 == '=') ||
          (c0 == '>' && c1 == '=') || (c0 == '<' && c1 == '=') ||
          (c0 == '&' && c1 == '&') || (c0 == '|' && c1 == '|')) {
        tokens[count].type = TOK_OP2;
        tokens[count].strVal[0] = c0;
        tokens[count].strVal[1] = c1;
        tokens[count].strVal[2] = '\0';
        count++;
        i += 2;
        continue;
      }
    }

    // Single-char operators (including comma for function args)
    char ch = expr[i];
    if (ch == '<' || ch == '>' || ch == '(' || ch == ')' ||
        ch == '+' || ch == '-' || ch == '*' || ch == ',') {
      tokens[count].type = TOK_OP1;
      tokens[count].strVal[0] = ch;
      tokens[count].strVal[1] = '\0';
      count++;
      i++;
      continue;
    }

    // Hex literal: #$XX or $XXXX
    if (ch == '#' && i + 1 < len && expr[i + 1] == '$') {
      i += 2;
      int32_t val = 0;
      while (i < len && isxdigit(static_cast<unsigned char>(expr[i]))) {
        int digit;
        char c = expr[i];
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else digit = c - 'A' + 10;
        val = (val << 4) | digit;
        i++;
      }
      tokens[count].type = TOK_NUM;
      tokens[count].numVal = val;
      count++;
      continue;
    }

    if (ch == '$') {
      i++;
      int32_t val = 0;
      while (i < len && isxdigit(static_cast<unsigned char>(expr[i]))) {
        int digit;
        char c = expr[i];
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else digit = c - 'A' + 10;
        val = (val << 4) | digit;
        i++;
      }
      tokens[count].type = TOK_NUM;
      tokens[count].numVal = val;
      count++;
      continue;
    }

    // Decimal literal
    if (isdigit(static_cast<unsigned char>(ch))) {
      int32_t val = 0;
      while (i < len && isdigit(static_cast<unsigned char>(expr[i]))) {
        val = val * 10 + (expr[i] - '0');
        i++;
      }
      tokens[count].type = TOK_NUM;
      tokens[count].numVal = val;
      count++;
      continue;
    }

    // Identifiers
    if (isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
      int start = i;
      while (i < len && (isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) {
        i++;
      }
      int idLen = i - start;
      if (idLen > 7) idLen = 7; // Truncate to fit strVal
      tokens[count].type = TOK_ID;
      for (int j = 0; j < idLen; j++) {
        tokens[count].strVal[j] = toupper(static_cast<unsigned char>(expr[start + j]));
      }
      tokens[count].strVal[idLen] = '\0';
      count++;
      continue;
    }

    // Unknown char, skip
    i++;
  }

  // End sentinel
  tokens[count].type = TOK_END;
  return count;
}

MachineView ConditionEvaluator::viewOf(const Emulator& emu) {
  MachineView view;
  // A //e's addresses are sixteen bits, so the bank a condition may have
  // written is ignored rather than reaching memory that does not exist.
  view.peek = [&emu](uint32_t address) {
    return emu.peekMemory(static_cast<uint16_t>(address & 0xFFFF));
  };
  view.pc = emu.getPC();
  view.a = emu.getA();
  view.x = emu.getX();
  view.y = emu.getY();
  view.sp = emu.getSP();
  view.p = emu.getP();
  return view;
}

bool ConditionEvaluator::evaluate(const char* expr, const MachineView& view) {
  errorBuf_[0] = '\0';
  ParseState s;
  s.count = tokenize(expr, s.tokens, MAX_TOKENS);
  s.pos = 0;
  s.view = &view;

  bool result = parseOr(s);
  return result;
}

int32_t ConditionEvaluator::evaluateNumeric(const char* expr,
                                            const MachineView& view) {
  errorBuf_[0] = '\0';
  ParseState s;
  s.count = tokenize(expr, s.tokens, MAX_TOKENS);
  s.pos = 0;
  s.view = &view;

  return parseExpr(s);
}

bool ConditionEvaluator::evaluate(const char* expr, const Emulator& emu) {
  const MachineView view = viewOf(emu);
  return evaluate(expr, view);
}

int32_t ConditionEvaluator::evaluateNumeric(const char* expr,
                                            const Emulator& emu) {
  const MachineView view = viewOf(emu);
  return evaluateNumeric(expr, view);
}

const char* ConditionEvaluator::getLastError() {
  return errorBuf_;
}

bool ConditionEvaluator::parseOr(ParseState& s) {
  bool left = parseAnd(s);
  while (s.pos < s.count && s.tokens[s.pos].type == TOK_OP2 &&
         s.tokens[s.pos].strVal[0] == '|') {
    s.pos++;
    bool right = parseAnd(s);
    left = left || right;
  }
  return left;
}

bool ConditionEvaluator::parseAnd(ParseState& s) {
  bool left = parseComparison(s);
  while (s.pos < s.count && s.tokens[s.pos].type == TOK_OP2 &&
         s.tokens[s.pos].strVal[0] == '&') {
    s.pos++;
    bool right = parseComparison(s);
    left = left && right;
  }
  return left;
}

bool ConditionEvaluator::parseComparison(ParseState& s) {
  int32_t left = parseExpr(s);

  if (s.pos < s.count) {
    const Token& tok = s.tokens[s.pos];
    if (tok.type == TOK_OP2 || tok.type == TOK_OP1) {
      const char* op = tok.strVal;
      if (strcmp(op, "==") == 0) { s.pos++; return left == parseExpr(s); }
      if (strcmp(op, "!=") == 0) { s.pos++; return left != parseExpr(s); }
      if (strcmp(op, ">=") == 0) { s.pos++; return left >= parseExpr(s); }
      if (strcmp(op, "<=") == 0) { s.pos++; return left <= parseExpr(s); }
      if (strcmp(op, ">") == 0)  { s.pos++; return left > parseExpr(s); }
      if (strcmp(op, "<") == 0)  { s.pos++; return left < parseExpr(s); }
    }
  }

  return left != 0; // Truthy if no comparison
}

int32_t ConditionEvaluator::parseExpr(ParseState& s) {
  int32_t val = parseAtom(s);

  while (s.pos < s.count) {
    const Token& tok = s.tokens[s.pos];
    if (tok.type == TOK_OP1) {
      char op = tok.strVal[0];
      if (op == '+') { s.pos++; val += parseAtom(s); }
      else if (op == '-') { s.pos++; val -= parseAtom(s); }
      else if (op == '*') { s.pos++; val *= parseAtom(s); }
      else break;
    } else break;
  }

  return val;
}

int32_t ConditionEvaluator::parseAtom(ParseState& s) {
  if (s.pos >= s.count) return 0;

  const Token& t = s.tokens[s.pos];

  // Number literal
  if (t.type == TOK_NUM) {
    s.pos++;
    return t.numVal;
  }

  // Parenthesized expression
  if (t.type == TOK_OP1 && t.strVal[0] == '(') {
    s.pos++;
    int32_t val = static_cast<int32_t>(parseOr(s));
    if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
        s.tokens[s.pos].strVal[0] == ')') {
      s.pos++;
    }
    return val;
  }

  // Identifier
  if (t.type == TOK_ID) {
    s.pos++;
    const char* id = t.strVal;
    const MachineView& emu = *s.view;

    // BV(b1,b2) - read BASIC simple variable value by encoded name bytes
    if (strcmp(id, "BV") == 0 && s.pos < s.count &&
        s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == '(') {
      s.pos++; // skip '('
      int32_t b1 = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == ',') s.pos++;
      int32_t b2 = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
          s.tokens[s.pos].strVal[0] == ')') {
        s.pos++;
      }
      return readBasicVariable(emu, static_cast<uint8_t>(b1), static_cast<uint8_t>(b2));
    }

    // BA(b1,b2,idx) - read BASIC array element by encoded name bytes and flat index
    if (strcmp(id, "BA") == 0 && s.pos < s.count &&
        s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == '(') {
      s.pos++; // skip '('
      int32_t b1 = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == ',') s.pos++;
      int32_t b2 = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == ',') s.pos++;
      int32_t idx = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
          s.tokens[s.pos].strVal[0] == ')') {
        s.pos++;
      }
      return readBasicArrayElement(emu, static_cast<uint8_t>(b1), static_cast<uint8_t>(b2), idx);
    }

    // BA2(b1,b2,i1,i2) - read BASIC 2D array element
    if (strcmp(id, "BA2") == 0 && s.pos < s.count &&
        s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == '(') {
      s.pos++; // skip '('
      int32_t b1 = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == ',') s.pos++;
      int32_t b2 = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == ',') s.pos++;
      int32_t i1 = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == ',') s.pos++;
      int32_t i2 = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
          s.tokens[s.pos].strVal[0] == ')') {
        s.pos++;
      }
      return readBasicArrayElement2D(emu, static_cast<uint8_t>(b1), static_cast<uint8_t>(b2), i1, i2);
    }

    // PEEK(addr) - read byte
    if (strcmp(id, "PEEK") == 0 && s.pos < s.count &&
        s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == '(') {
      s.pos++;
      int32_t addr = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
          s.tokens[s.pos].strVal[0] == ')') {
        s.pos++;
      }
      return emu.peek(static_cast<uint16_t>(addr & 0xFFFF));
    }

    // DEEK(addr) - read 16-bit word (little-endian)
    if (strcmp(id, "DEEK") == 0 && s.pos < s.count &&
        s.tokens[s.pos].type == TOK_OP1 && s.tokens[s.pos].strVal[0] == '(') {
      s.pos++;
      int32_t addr = parseExpr(s);
      if (s.pos < s.count && s.tokens[s.pos].type == TOK_OP1 &&
          s.tokens[s.pos].strVal[0] == ')') {
        s.pos++;
      }
      uint8_t lo = emu.peek(static_cast<uint16_t>(addr & 0xFFFF));
      uint8_t hi = emu.peek(static_cast<uint16_t>((addr + 1) & 0xFFFF));
      return (hi << 8) | lo;
    }

    // Registers
    if (strcmp(id, "A") == 0)  return emu.a;
    if (strcmp(id, "X") == 0)  return emu.x;
    if (strcmp(id, "Y") == 0)  return emu.y;
    if (strcmp(id, "SP") == 0) return emu.sp;
    if (strcmp(id, "PC") == 0) return emu.pc;
    if (strcmp(id, "P") == 0)  return emu.p;

    // Individual flags
    if (strcmp(id, "C") == 0) return (emu.p & 0x01) ? 1 : 0;
    if (strcmp(id, "Z") == 0) return (emu.p & 0x02) ? 1 : 0;
    if (strcmp(id, "I") == 0) return (emu.p & 0x04) ? 1 : 0;
    if (strcmp(id, "D") == 0) return (emu.p & 0x08) ? 1 : 0;
    if (strcmp(id, "B") == 0) return (emu.p & 0x10) ? 1 : 0;
    if (strcmp(id, "V") == 0) return (emu.p & 0x40) ? 1 : 0;
    if (strcmp(id, "N") == 0) return (emu.p & 0x80) ? 1 : 0;

    snprintf(errorBuf_, sizeof(errorBuf_), "Unknown identifier: %s", id);
    return 0;
  }

  // Fallback
  s.pos++;
  return 0;
}

} // namespace a2e
