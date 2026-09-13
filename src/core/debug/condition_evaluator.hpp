/*
 * condition_evaluator.hpp - Expression parser for debugger breakpoint conditions and watches
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <functional>
#include <cstring>

namespace a2e {

class Emulator;

/**
 * Evaluates breakpoint condition expressions.
 *
 * Syntax:
 *   Registers: A, X, Y, SP, PC, P
 *   Flags: C, Z, I, D, B, V, N (return 0 or 1)
 *   Memory: PEEK($addr), DEEK($addr) (16-bit little-endian)
 *   Hex literals: #$FF, $FFFF
 *   Decimal literals: 42
 *   BASIC vars: BV(b1,b2) - simple variable, BA(b1,b2,idx) - array element
 *     b1,b2 are the encoded name bytes; type bits in high bits
 *   Comparisons: ==, !=, >=, <=, >, <
 *   Arithmetic: +, -, *
 *   Logic: &&, ||
 *   Grouping: ( )
 */
/**
 * What a condition can ask about a running machine.
 *
 * The evaluator used to take a `const Emulator&`, which meant a condition
 * could only ever be asked of a //e — so a conditional breakpoint on a IIgs
 * silently never fired. Everything it actually needs is in here: a way to
 * peek a byte, and the processor's registers. A machine builds one of these
 * and the evaluator asks no further questions about which machine it is.
 *
 * Addresses are 24-bit, and the registers are as wide as the widest
 * processor's — a 6502's fill the low halves.
 */
struct MachineView {
  std::function<uint8_t(uint32_t)> peek;
  uint32_t pc = 0;
  uint16_t a = 0, x = 0, y = 0, sp = 0;
  uint8_t p = 0;
};

class ConditionEvaluator {
public:
  /**
   * Evaluate a condition expression as a boolean.
   * Returns true if the condition is satisfied.
   */
  static bool evaluate(const char* expr, const MachineView& view);

  /** The same, of a //e: builds the view from the emulator. */
  static bool evaluate(const char* expr, const Emulator& emu);

  /**
   * Evaluate an expression and return the raw numeric value.
   * Used for watch expressions.
   */
  static int32_t evaluateNumeric(const char* expr, const MachineView& view);
  static int32_t evaluateNumeric(const char* expr, const Emulator& emu);

  /** A view of a //e, for the two overloads above and for any other caller. */
  static MachineView viewOf(const Emulator& emu);

  /**
   * Get the last error message (empty string if no error).
   */
  static const char* getLastError();

private:
  // Token types
  enum TokenType : uint8_t {
    TOK_NUM,     // Numeric literal
    TOK_ID,      // Identifier (register, PEEK, etc.)
    TOK_OP2,     // Two-character operator (==, !=, >=, <=, &&, ||)
    TOK_OP1,     // Single-character operator (<, >, +, -, *, (, ))
    TOK_END      // End of tokens
  };

  struct Token {
    TokenType type;
    int32_t numVal;     // For TOK_NUM
    char strVal[8];     // For TOK_ID or TOK_OP (null-terminated)
  };

  static constexpr int MAX_TOKENS = 128;

  struct ParseState {
    Token tokens[MAX_TOKENS];
    int count;
    int pos;
    const MachineView* view;
  };

  static int tokenize(const char* expr, Token* tokens, int maxTokens);
  static bool parseOr(ParseState& s);
  static bool parseAnd(ParseState& s);
  static bool parseComparison(ParseState& s);
  static int32_t parseExpr(ParseState& s);
  static int32_t parseAtom(ParseState& s);

  static char errorBuf_[128];
};

} // namespace a2e
