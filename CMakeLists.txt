cmake_minimum_required(VERSION 3.20)
project(web-a2e VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source files
set(CORE_SOURCES
    src/core/emulator.cpp
    src/core/debug/machine_debug.cpp
    src/core/emulator/emulator_state.cpp
    src/core/emulator/emulator_debug.cpp
    src/core/cpu/6502/cpu6502.cpp
    src/core/mmu/mmu.cpp
    src/core/video/video.cpp
    src/core/video/ntsc.cpp
    src/core/audio/audio.cpp
    src/core/disk-image/dsk_disk_image.cpp
    src/core/disk-image/woz_disk_image.cpp
    src/core/disk-image/disk_converter.cpp
    src/core/disk-image/gcr_encoding.cpp
    src/core/input/joyport.cpp
    src/core/input/mouse_iou.cpp
    src/core/input/keyboard.cpp
    src/core/disassembler/disassembler.cpp
    src/core/disassembler/disassembler65816.cpp
    src/core/cards/mockingboard/ay8910.cpp
    src/core/debug/debug_log.cpp
    src/core/cards/mockingboard/via6522.cpp
    src/core/cards/mockingboard/mockingboard_card.cpp
    src/core/iigs/iigs_memory.cpp
    src/core/iigs/iigs_clock.cpp
    src/core/iigs/iigs_machine.cpp
    src/core/iigs/iigs_state.cpp
    src/core/disassembler/disassembler65816.cpp
    src/core/iigs/iigs_sound.cpp
    src/core/iigs/iigs_video.cpp
    src/core/iigs/iigs_adb.cpp
    src/core/iigs/iigs_scc.cpp
    src/core/cpu/65816/cpu65816.cpp
    src/core/cpu/65816/cpu65816_dispatch.cpp
    src/core/cards/disk_controller.cpp
    src/core/cards/disk2/disk2_card.cpp
    src/core/cards/iwm/iwm.cpp
    src/core/cards/thunderclock/thunderclock_card.cpp
    src/core/cards/mouse/mouse_card.cpp
    src/core/cards/smartport/block_device.cpp
    src/core/cards/smartport/smartport_card.cpp
    src/core/cards/softcard/softcard_z80.cpp
    src/core/cards/ssc/acia6551.cpp
    src/core/cards/serial/serial_port.cpp
    src/core/cards/ssc/ssc_card.cpp
    src/core/cards/parallel/parallel_card.cpp
    src/core/cards/softcard/z80/z80.cpp
    src/core/cards/softcard/z80/z80_opcode_tables.cpp
    src/core/cards/softcard/z80/z80_opcodes_main.cpp
    src/core/cards/softcard/z80/z80_opcodes_cb.cpp
    src/core/cards/softcard/z80/z80_opcodes_dd.cpp
    src/core/cards/softcard/z80/z80_opcodes_ed.cpp
    src/core/cards/softcard/z80/z80_opcodes_fd.cpp
    src/core/cards/softcard/z80/z80_opcodes_ddcb.cpp
    src/core/cards/softcard/z80/z80_opcodes_fdcb.cpp
    src/core/noslot_clock.cpp
    src/core/basic/applesoft_vars.cpp
    src/core/debug/condition_evaluator.cpp
    src/core/filesystem/dos33.cpp
    src/core/filesystem/prodos.cpp
    src/core/filesystem/pascal.cpp
    src/core/basic/basic_detokenizer.cpp
    src/core/basic/basic_tokenizer.cpp
    src/core/assembler/assembler.cpp
    src/bindings/wasm_interface.cpp
)

# ROM embedding - generate C arrays from ROM files
set(ROM_FILES
    roms/342-0349-B-C0-FF.bin                       # Combined 16KB system ROM (C0-FF)
    roms/342-0273-A-US-UK.bin                       # Character ROM (Apple IIe Enhanced)
    roms/341-0027.bin                               # Disk II ROM
    "roms/Thunderclock Plus ROM.bin"                 # Thunderclock card ROM
    "roms/Apple Mouse Interface Card ROM - 342-0270-C.bin"  # Mouse Interface Card ROM
    "roms/Super Serial Card ROM - 341-0065-A.bin"           # Super Serial Card ROM
    "roms/Apple Parallel Interface Card ROM - 341-0057.bin" # Parallel Interface Card PROM (341-0005/341-0019)
)

# Apple II Plus ROMs are optional — the generator emits empty arrays when they
# are missing, and the II+ profile then reports having no ROM. Listed separately
# so a missing file is not a broken dependency for the //e build.
set(ROM_FILES_OPTIONAL_CANDIDATES
    roms/341-0011.bin   # Applesoft $D000-$D7FF
    roms/341-0012.bin   # Applesoft $D800-$DFFF
    roms/341-0013.bin   # Applesoft $E000-$E7FF
    roms/341-0014.bin   # Applesoft $E800-$EFFF
    roms/341-0015.bin   # Applesoft $F000-$F7FF
    roms/341-0020.bin   # Autostart monitor $F800-$FFFF
    roms/apple2plus.rom # ...or one combined 12KB image instead of the six
    roms/341-0036.bin   # II+ character generator
)

# Only the candidates that are actually present become build dependencies. A
# DEPENDS entry with no rule to build it is a hard error, and these are
# alternatives — supplying the six motherboard ROMs means apple2plus.rom will
# never exist, and vice versa. Re-run cmake after adding ROMs.
set(ROM_FILES_OPTIONAL "")
foreach(candidate ${ROM_FILES_OPTIONAL_CANDIDATES})
    if(EXISTS ${CMAKE_SOURCE_DIR}/${candidate})
        list(APPEND ROM_FILES_OPTIONAL ${candidate})
    endif()
endforeach()

# Custom command to generate ROM header
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/roms.cpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/generate_roms.sh ${CMAKE_SOURCE_DIR}/roms ${CMAKE_BINARY_DIR}/generated/roms.cpp
    # ROM_FILES_OPTIONAL must be listed too. Without it, dropping the II+ ROMs
    # into roms/ changed nothing the build was watching, so roms.cpp stayed as
    # it was and the machine reported having no ROM long after its ROM existed.
    DEPENDS ${ROM_FILES} ${ROM_FILES_OPTIONAL}
            ${CMAKE_SOURCE_DIR}/scripts/generate_roms.sh
    COMMENT "Generating ROM data arrays"
)

if(EMSCRIPTEN)
    add_executable(a2e
        ${CORE_SOURCES}
    )

    # Ensure roms.cpp is generated before building
    add_custom_target(generate_roms DEPENDS ${CMAKE_BINARY_DIR}/generated/roms.cpp)
    add_dependencies(a2e generate_roms)

    target_include_directories(a2e PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/cpu/6502
        ${CMAKE_SOURCE_DIR}/src/core/cpu/65816
        ${CMAKE_SOURCE_DIR}/src/core/cards
        ${CMAKE_SOURCE_DIR}/src/core/disk-image
        ${CMAKE_BINARY_DIR}/generated
    )

    # Emscripten-specific flags
    # MAXIMUM_MEMORY is 512MB. The ceiling only has to hold the largest machine
    # anyone can configure, and that is a IIgs with 8MB of fast RAM and two 32MB
    # SmartPort volumes: about 72MB live, and a save state is a second copy of
    # the images on top. At 256MB that aborted the module outright — and an
    # aborted module answers nothing afterwards, so the symptom was every
    # control in the app going dead rather than one save failing. Growth is
    # still on, so the ceiling costs nothing until it is needed.
    set_target_properties(a2e PROPERTIES
        SUFFIX ".js"
        LINK_FLAGS "\
            -s WASM=1 \
            -s MODULARIZE=1 \
            -s EXPORT_NAME='createA2EModule' \
            -s EXPORTED_FUNCTIONS='[ \
                \"_init\", \
                \"_reset\", \
                \"_warmReset\", \
                \"_runCycles\", \
                \"_consumeFrameSamples\", \
                \"_getFramebuffer\", \
                \"_getFramebufferSize\", \
                \"_getMachineCount\", \
                \"_getMachineKey\", \
                \"_getMachineKeyAt\", \
                \"_getMachineName\", \
                \"_getMachineProfileJSON\", \
                \"_getMachineProfileJSONAt\", \
                \"_hasSystemROM\", \
                \"_isMachineRunnable\", \
                \"_setMachine\", \
                \"_getIIgsMemoryKB\", \
                \"_setIIgsMemoryKB\", \
                \"_forceRenderFrame\", \
                \"_isFrameReady\", \
                \"_keyDown\", \
                \"_keyUp\", \
                \"_handleRawKeyDown\", \
                \"_handleRawKeyUp\", \
                \"_releaseModifiers\", \
                \"_charToAppleKey\", \
                \"_pasteText\", \
                \"_pasteKey\", \
                \"_pastePending\", \
                \"_clearPasteBuffer\", \
                \"_insertDisk\", \
                \"_insertBlankDisk\", \
                \"_ejectDisk\", \
                \"_getDiskData\", \
                \"_getDiskDataAs\", \
                \"_getDiskSectorDataDOSOrder\", \
                \"_canSaveDiskAs\", \
                \"_getDiskNativeFormat\", \
                \"_addBreakpoint\", \
                \"_removeBreakpoint\", \
                \"_enableBreakpoint\", \
                \"_isBreakpointHit\", \
                \"_getBreakpointAddress\", \
                \"_addBasicBreakpoint\", \
                \"_removeBasicBreakpoint\", \
                \"_clearBasicBreakpoints\", \
                \"_clearBasicBreakpointHit\", \
                \"_addBasicConditionRule\", \
                \"_removeBasicConditionRule\", \
                \"_clearBasicConditionRules\", \
                \"_getBasicConditionRuleHitId\", \
                \"_hasBasicBreakpoints\", \
                \"_isBasicBreakpointHit\", \
                \"_getBasicBreakLine\", \
                \"_isBasicProgramRunning\", \
                \"_stepBasicLine\", \
                \"_stepBasicStatement\", \
                \"_getBasicTxtptr\", \
                \"_getBasicStatementIndex\", \
                \"_getBasicStatementCountForLine\", \
                \"_getBasicStatementIndexForLine\", \
                \"_setBasicHeatMapEnabled\", \
                \"_clearBasicHeatMap\", \
                \"_getBasicHeatMapSize\", \
                \"_getBasicHeatMapData\", \
                \"_getBasicLineBytes\", \
                \"_getPC\", \
                \"_getPBR\", \
                \"_getDBR\", \
                \"_getDirectPage\", \
                \"_getCpuWidths\", \
                \"_setRegPBR\", \
                \"_setRegDBR\", \
                \"_setRegDirectPage\", \
                \"_getTraceEntrySize\", \
                \"_formatTraceRange\", \
                \"_getMemoryBanksJSON\", \
                \"_getBatteryRam\", \
                \"_getBatteryRamSize\", \
                \"_setBatteryRam\", \
                \"_batteryRamChanged\", \
                \"_setIIgsLoopbackCable\", \
                \"_hasIIgsLoopbackCable\", \
                \"_getSP\", \
                \"_getA\", \
                \"_getX\", \
                \"_getY\", \
                \"_getP\", \
                \"_getTotalCycles\", \
                \"_isPaused\", \
                \"_setPaused\", \
                \"_stepInstruction\", \
                \"_readMemory\", \
                \"_writeMemory\", \
                \"_disassembleAt\", \
                \"_disassembleRange\", \
                \"_getSoftSwitchState\", \
                \"_screenCodeToAscii\", \
                \"_readScreenText\", \
                \"_getDiskTrack\", \
                \"_getDiskPhase\", \
                \"_getDiskMotorOn\", \
                \"_getDiskWriteMode\", \
                \"_getDiskHeadPosition\", \
                \"_getSelectedDrive\", \
                \"_isDiskInserted\", \
                \"_isDiskModified\", \
                \"_getDiskFilename\", \
                \"_getLastDiskByte\", \
                \"_exportState\", \
                \"_importState\", \
                \"_setMonochrome\", \
                \"_isMonochrome\", \
                \"_setVideoColorMode\", \
                \"_getVideoColorMode\", \
                \"_disassembleRawData\", \
                \"_getDisasmInstructions\", \
                \"_getDisasmInstructionLength\", \
                \"_malloc\", \
                \"_free\", \
                \"_getSlotCard\", \
                \"_setSlotCard\", \
                \"_isSlotInternal\", \
                \"_setSlotInternal\", \
                \"_isSlotEmpty\", \
                \"_getMockingboardWaveform\", \
                \"_setMockingboardChannelMute\", \
                \"_getMockingboardChannelMute\", \
                \"_isKeyboardReady\", \
                \"_setSpeedMultiplier\", \
                \"_getSpeedMultiplier\", \
                \"_mouseMove\", \
                \"_mouseButton\", \
                \"_isMouseCardInstalled\", \
                \"_getMouseCardState\", \
                \"_getMouseCardPIARegister\", \
                \"_isIRQPending\", \
                \"_isNMIPending\", \
                \"_isNMIEdge\", \
                \"_setRegA\", \
                \"_setRegX\", \
                \"_setRegY\", \
                \"_setRegSP\", \
                \"_setRegPC\", \
                \"_setRegP\", \
                \"_peekMemory\", \
                \"_readMainRAM\", \
                \"_addWatchpoint\", \
                \"_removeWatchpoint\", \
                \"_clearWatchpoints\", \
                \"_isWatchpointHit\", \
                \"_getWatchpointAddress\", \
                \"_getWatchpointValue\", \
                \"_isWatchpointWrite\", \
                \"_setTraceEnabled\", \
                \"_clearTrace\", \
                \"_getTraceCount\", \
                \"_getTraceHead\", \
                \"_getTraceBuffer\", \
                \"_getTraceCapacity\", \
                \"_setProfileEnabled\", \
                \"_clearProfile\", \
                \"_getProfileCycles\", \
                \"_addBeamBreakpoint\", \
                \"_removeBeamBreakpoint\", \
                \"_enableBeamBreakpoint\", \
                \"_clearAllBeamBreakpoints\", \
                \"_isBeamBreakpointHit\", \
                \"_getBeamBreakpointHitId\", \
                \"_getBeamBreakScanline\", \
                \"_getBeamBreakHPos\", \
                \"_getFrameCycle\", \
                \"_getBeamScanline\", \
                \"_getBeamHPos\", \
                \"_getBeamColumn\", \
                \"_isInVBL\", \
                \"_isInHBLANK\", \
                \"_stepOver\", \
                \"_stepOut\", \
                \"_clearTempBreakpoint\", \
                \"_isTempBreakpointHit\", \
                \"_getOpcodeMnemonic\", \
                \"_getOpcodeAddressingMode\", \
                \"_getCallStack\", \
                \"_getCallStackBuffer\", \
                \"_isLikelyReturnAddress\", \
                \"_evaluateCondition\", \
                \"_evaluateExpression\", \
                \"_getConditionError\", \
                \"_isDOS33Format\", \
                \"_getDOS33Catalog\", \
                \"_getDOS33CatalogBuffer\", \
                \"_getDOS33CatalogEntrySize\", \
                \"_getDOS33EntryFilename\", \
                \"_getDOS33EntryFileType\", \
                \"_getDOS33EntryFileTypeName\", \
                \"_getDOS33EntryIsLocked\", \
                \"_getDOS33EntrySectorCount\", \
                \"_readDOS33File\", \
                \"_getDOS33FileBuffer\", \
                \"_isProDOSFormat\", \
                \"_getProDOSVolumeInfo\", \
                \"_getProDOSVolumeName\", \
                \"_getProDOSTotalBlocks\", \
                \"_getProDOSCatalog\", \
                \"_getProDOSEntryFilename\", \
                \"_getProDOSEntryPath\", \
                \"_getProDOSEntryFileType\", \
                \"_getProDOSEntryFileTypeName\", \
                \"_getProDOSEntryStorageType\", \
                \"_getProDOSEntryEOF\", \
                \"_getProDOSEntryAuxType\", \
                \"_getProDOSEntryIsLocked\", \
                \"_getProDOSEntryBlocksUsed\", \
                \"_getProDOSEntryIsDirectory\", \
                \"_getProDOSEntryKeyPointer\", \
                \"_getProDOSDirectory\", \
                \"_readProDOSFile\", \
                \"_getProDOSFileBuffer\", \
                \"_mapProDOSFileType\", \
                \"_isPascalFormat\", \
                \"_getPascalVolumeInfo\", \
                \"_getPascalVolumeName\", \
                \"_getPascalTotalBlocks\", \
                \"_getPascalCatalog\", \
                \"_getPascalEntryFilename\", \
                \"_getPascalEntryFileType\", \
                \"_getPascalEntryFileTypeName\", \
                \"_getPascalEntryFileSize\", \
                \"_getPascalEntryBlocksUsed\", \
                \"_readPascalFile\", \
                \"_getPascalFileBuffer\", \
                \"_mapPascalFileType\", \
                \"_detokenizeApplesoft\", \
                \"_detokenizeIntegerBasic\", \
                \"_assembleSource\", \
                \"_getAsmOutputSize\", \
                \"_getAsmOutputBuffer\", \
                \"_getAsmOrigin\", \
                \"_getAsmErrorCount\", \
                \"_getAsmErrorLine\", \
                \"_getAsmErrorMessage\", \
                \"_getAsmErrorIsWarning\", \
                \"_getAsmSegmentCount\", \
                \"_getAsmSegmentAddress\", \
                \"_getAsmSegmentOffset\", \
                \"_getAsmSegmentLength\", \
                \"_getAsmLineCount\", \
                \"_getAsmLineRecordSize\", \
                \"_getAsmLineBuffer\", \
                \"_getAsmListing\", \
                \"_getAsmObjectType\", \
                \"_loadAsmIntoMemory\", \
                \"_getAsmSymbolCount\", \
                \"_getAsmSymbolName\", \
                \"_getAsmSymbolValue\", \
                \"_hasAsmObjectFile\", \
                \"_getAsmObjectFilename\", \
                \"_getAsmObjectDrive\", \
                \"_writeAsmObjectToDisk\", \
                \"_getAsmObjectStatusMessage\", \
                \"_isBasicErrorHit\", \
                \"_getBasicErrorLine\", \
                \"_getBasicErrorTxtptr\", \
                \"_getBasicErrorCode\", \
                \"_clearBasicError\", \
                \"_insertSmartPortImage\", \
                \"_ejectSmartPortImage\", \
                \"_isSmartPortImageInserted\", \
                \"_getSmartPortImageFilename\", \
                \"_isSmartPortImageModified\", \
                \"_getSmartPortImageData\", \
                \"_isSmartPortCardInstalled\", \
                \"_getSmartPortBlockData\", \
                \"_getSmartPortActivity\", \
                \"_getSmartPortActivityWrite\", \
                \"_clearSmartPortActivity\", \
                \"_loadBasicProgram\", \
                \"_refreshBasicVariables\", \
                \"_getBasicVariableName\", \
                \"_getBasicVariableType\", \
                \"_getBasicVariableAddress\", \
                \"_getBasicVariableReal\", \
                \"_getBasicVariableInt\", \
                \"_getBasicVariableString\", \
                \"_refreshBasicArrays\", \
                \"_getBasicArrayName\", \
                \"_getBasicArrayType\", \
                \"_getBasicArrayAddress\", \
                \"_getBasicArrayNumDims\", \
                \"_getBasicArrayDim\", \
                \"_getBasicArrayElementCount\", \
                \"_getBasicArrayReals\", \
                \"_getBasicArrayInts\", \
                \"_getBasicArrayStrings\", \
                \"_getBasicArrayStringsSize\", \
                \"_writeApplesoftFloat\", \
                \"_generateStereoAudioSamples\", \
                \"_setAudioVolume\", \
                \"_setAudioMuted\", \
                \"_setButton\", \
                \"_setPaddleValue\", \
                \"_setGamePortDevice\", \
                \"_getGamePortDevice\", \
                \"_setJoyportStick\", \
                \"_getJoyportStick\", \
                \"_getPaddleValue\", \
                \"_getDiskSectorData\", \
                \"_stopDiskMotor\", \
                \"_getTrackNibble\", \
                \"_getTrackNibbleCount\", \
                \"_getCurrentNibblePosition\", \
                \"_peekAuxMemory\", \
                \"_enableMemoryTracking\", \
                \"_clearMemoryTracking\", \
                \"_decayMemoryTracking\", \
                \"_getMemoryReadCounts\", \
                \"_getMemoryWriteCounts\", \
                \"_getMainRAM\", \
                \"_getAuxRAM\", \
                \"_getSystemROM\", \
                \"_setUKCharacterSet\", \
                \"_isUKCharacterSet\", \
                \"_getSoftSwitchStateHigh\", \
                \"_disassembleWithFlowAnalysis\", \
                \"_disassembleWithFlowAnalysisMultiEntry\", \
                \"_getBasicDebugInfo\", \
                \"_isMockingboardEnabled\", \
                \"_getMockingboardPSGRegister\", \
                \"_getMockingboardPSGRegisters\", \
                \"_getMockingboardVIAIRQ\", \
                \"_getMockingboardVIAPort\", \
                \"_getMockingboardPSGWriteInfo\", \
                \"_getMockingboardVIATimerInfo\", \
                \"_setMockingboardDebugLogging\", \
                \"_enableNoSlotClock\", \
                \"_isNoSlotClockEnabled\", \
                \"_serialReceive\", \
                \"_isSSCInstalled\", \
                \"_setSerialTxCallback\", \
                \"_isParallelCardInstalled\", \
                \"_setParallelTxCallback\" \
            ]' \
            -s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\", \"UTF8ToString\", \"stringToUTF8\", \"HEAPU8\", \"HEAPF32\"]' \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s INITIAL_MEMORY=33554432 \
            -s MAXIMUM_MEMORY=536870912 \
            -s NO_EXIT_RUNTIME=1 \
            -s ASYNCIFY=0 \
            --no-entry \
            -O3 \
            -flto \
        "
    )

    # Copy output to public directory
    add_custom_command(TARGET a2e POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:a2e> ${CMAKE_SOURCE_DIR}/public/a2e.js
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/a2e.wasm ${CMAKE_SOURCE_DIR}/public/a2e.wasm
        COMMENT "Copying WASM files to public directory"
    )

else()
    # Native build for testing
    # Note: a2e-native requires src/native/main.cpp which may not exist
    # The tests below can be built independently

    enable_testing()

    # ==========================================================================
    # Common paths and source lists
    # ==========================================================================

    set(CATCH2_INCLUDE ${CMAKE_SOURCE_DIR}/tests/catch2)
    set(TEST_COMMON_INCLUDE ${CMAKE_SOURCE_DIR}/tests/common)

    # All core include directories
    set(ALL_CORE_INCLUDES
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/cpu/6502
        ${CMAKE_SOURCE_DIR}/src/core/cpu/65816
        ${CMAKE_SOURCE_DIR}/src/core/machine
        ${CMAKE_SOURCE_DIR}/src/core/iigs
        ${CMAKE_SOURCE_DIR}/src/core/mmu
        ${CMAKE_SOURCE_DIR}/src/core/video
        ${CMAKE_SOURCE_DIR}/src/core/audio
        ${CMAKE_SOURCE_DIR}/src/core/input
        ${CMAKE_SOURCE_DIR}/src/core/cards
        ${CMAKE_SOURCE_DIR}/src/core/cards/mockingboard
        ${CMAKE_SOURCE_DIR}/src/core/cards/disk2
        ${CMAKE_SOURCE_DIR}/src/core/cards/iwm
        ${CMAKE_SOURCE_DIR}/src/core/cards/serial
        ${CMAKE_SOURCE_DIR}/src/core/cards/ssc
        ${CMAKE_SOURCE_DIR}/src/core/cards/thunderclock
        ${CMAKE_SOURCE_DIR}/src/core/cards/mouse
        ${CMAKE_SOURCE_DIR}/src/core/cards/softcard
        ${CMAKE_SOURCE_DIR}/src/core/cards/softcard/z80
        ${CMAKE_SOURCE_DIR}/src/core/cards/smartport
        ${CMAKE_SOURCE_DIR}/src/core/disk-image
        ${CMAKE_SOURCE_DIR}/src/core/disassembler
        ${CMAKE_SOURCE_DIR}/src/core/debug
        ${CMAKE_SOURCE_DIR}/src/core/filesystem
        ${CMAKE_SOURCE_DIR}/src/core/basic
        ${CMAKE_SOURCE_DIR}/src/core/assembler
        ${CMAKE_BINARY_DIR}/generated
    )

    # All core source files (excluding wasm_interface.cpp)
    set(CORE_TEST_SOURCES
        src/core/emulator.cpp
        src/core/debug/machine_debug.cpp
        src/core/emulator/emulator_state.cpp
        src/core/emulator/emulator_debug.cpp
        src/core/cpu/6502/cpu6502.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/input/joyport.cpp
        src/core/input/mouse_iou.cpp
        src/core/input/keyboard.cpp
        src/core/disassembler/disassembler.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/disk2/disk2_card.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/softcard/softcard_z80.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/serial/serial_port.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/softcard/z80/z80.cpp
        src/core/cards/softcard/z80/z80_opcode_tables.cpp
        src/core/cards/softcard/z80/z80_opcodes_main.cpp
        src/core/cards/softcard/z80/z80_opcodes_cb.cpp
        src/core/cards/softcard/z80/z80_opcodes_dd.cpp
        src/core/cards/softcard/z80/z80_opcodes_ed.cpp
        src/core/cards/softcard/z80/z80_opcodes_fd.cpp
        src/core/cards/softcard/z80/z80_opcodes_ddcb.cpp
        src/core/cards/softcard/z80/z80_opcodes_fdcb.cpp
        src/core/noslot_clock.cpp
        src/core/basic/applesoft_vars.cpp
        src/core/debug/condition_evaluator.cpp
        src/core/filesystem/dos33.cpp
        src/core/filesystem/prodos.cpp
        src/core/filesystem/pascal.cpp
        src/core/basic/basic_detokenizer.cpp
        src/core/basic/basic_tokenizer.cpp
        src/core/assembler/assembler.cpp
    )

    # Common test helper sources
    set(TEST_COMMON_SOURCES
        tests/common/disk_image_builder.cpp
        tests/common/basic_program_builder.cpp
    )

    # Ensure roms.cpp is generated for native builds
    add_custom_target(generate_roms_native DEPENDS ${CMAKE_BINARY_DIR}/generated/roms.cpp)

    # ==========================================================================
    # Helper function: add a Catch2 test with specified sources
    # ==========================================================================
    function(add_catch2_test TEST_NAME TEST_SOURCE)
        # Remaining arguments are extra source files
        set(EXTRA_SOURCES ${ARGN})

        add_executable(${TEST_NAME}
            ${TEST_SOURCE}
            ${EXTRA_SOURCES}
        )
        add_dependencies(${TEST_NAME} generate_roms_native)
        target_include_directories(${TEST_NAME} PRIVATE
            ${ALL_CORE_INCLUDES}
            ${CATCH2_INCLUDE}
            ${TEST_COMMON_INCLUDE}
        )
        target_compile_options(${TEST_NAME} PRIVATE -O2 -Wall -Wextra)
        add_test(NAME ${TEST_NAME}
            COMMAND ${TEST_NAME}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    endfunction()

    # ==========================================================================
    # Catch2 Unit Tests - Pure (no ROM dependencies)
    # ==========================================================================

    # Super Hi-Res, drawn and then looked at.
    add_catch2_test(test_iigs_video
        tests/unit/test_iigs_video.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/debug/machine_debug.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/input/keyboard.cpp
        src/core/input/joyport.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # The two devices a IIgs will not boot without.
    # Debugging a IIgs: breakpoints with banks, watchpoints on the processor's
    # own bus, a step over that knows a long call, a trace entry twice as wide.
    add_catch2_test(test_iigs_debug
        tests/unit/test_iigs_debug.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/debug/machine_debug.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/audio/audio.cpp
        src/core/input/keyboard.cpp
        src/core/input/joyport.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/disk-image/disk_converter.cpp
    )

    add_catch2_test(test_iigs_devices
        tests/unit/test_iigs_devices.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_clock.cpp
    )

    # A IIgs running its own ROM, which is every part of it at once.
    add_catch2_test(test_iigs_boot
        tests/integration/test_iigs_boot.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/debug/machine_debug.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/input/keyboard.cpp
        src/core/input/joyport.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # A IIgs saved and restored: the processor, both kinds of RAM, the devices.
    add_catch2_test(test_iigs_state
        tests/integration/test_iigs_state.cpp
        src/core/iigs/iigs_machine.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/parallel/parallel_card.cpp
        src/core/cards/ssc/ssc_card.cpp
        src/core/cards/ssc/acia6551.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/iigs/iigs_state.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/debug/machine_debug.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/audio/audio.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/input/keyboard.cpp
        src/core/input/joyport.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_video.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
        src/core/mmu/mmu.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # A IIgs's address space, which needs no CPU to test: banks, shadowing,
    # and the Mega II underneath it.
    add_catch2_test(test_iigs_memory
        tests/unit/test_iigs_memory.cpp
        src/core/iigs/iigs_memory.cpp
        src/core/iigs/iigs_clock.cpp
        src/core/iigs/iigs_sound.cpp
        src/core/iigs/iigs_adb.cpp
        src/core/iigs/iigs_scc.cpp
        src/core/mmu/mmu.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # The 65816 against 5.1 million recorded states from a real one. Skips
    # unless A2E_65816_VECTORS points at the vector files, which are 3GB and
    # are not in the repository.
    add_catch2_test(test_65816_vectors
        tests/conformance/test_65816_vectors.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
    )

    # The 65816, which needs nothing but itself: no machine, no MMU, no IIgs.
    add_catch2_test(test_cpu65816
        tests/unit/test_cpu65816.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
    )

    # CPU tests (only need cpu6502.cpp)
    add_catch2_test(test_cpu6502
        tests/unit/test_cpu6502.cpp
        src/core/cpu/6502/cpu6502.cpp
    )
    add_catch2_test(test_cpu_addressing
        tests/unit/test_cpu_addressing.cpp
        src/core/cpu/6502/cpu6502.cpp
    )
    add_catch2_test(test_cpu_interrupts
        tests/unit/test_cpu_interrupts.cpp
        src/core/cpu/6502/cpu6502.cpp
    )
    add_catch2_test(test_cpu_decimal
        tests/unit/test_cpu_decimal.cpp
        src/core/cpu/6502/cpu6502.cpp
    )
    add_catch2_test(test_cpu_cycle_counts
        tests/unit/test_cpu_cycle_counts.cpp
        src/core/cpu/6502/cpu6502.cpp
    )

    # GCR encoding (only gcr_encoding.cpp)
    add_catch2_test(test_gcr_encoding
        tests/unit/test_gcr_encoding.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # Disassembler (only disassembler.cpp)
    add_catch2_test(test_disassembler
        tests/unit/test_disassembler.cpp
        src/core/disassembler/disassembler.cpp
    )

    # The 65816 disassembler, checked against the 65816 itself.
    add_catch2_test(test_disassembler65816
        tests/unit/test_disassembler65816.cpp
        src/core/disassembler/disassembler65816.cpp
        src/core/cpu/65816/cpu65816.cpp
        src/core/cpu/65816/cpu65816_dispatch.cpp
    )

    # Assembler (only assembler.cpp + disassembler for opcode table)
    add_catch2_test(test_assembler
        tests/unit/test_assembler.cpp
        src/core/assembler/assembler.cpp
        src/core/disassembler/disassembler.cpp
    )

    # Audio (audio.cpp + mockingboard deps for consumeStereoSamples)
    add_catch2_test(test_audio
        tests/unit/test_audio.cpp
        src/core/audio/audio.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
    )

    # Keyboard (only keyboard.cpp)
    add_catch2_test(test_keyboard
        tests/unit/test_keyboard.cpp
        src/core/input/keyboard.cpp
    )

    # A //c's IOU mouse (only mouse_iou.cpp)
    add_catch2_test(test_mouse_iou
        tests/unit/test_mouse_iou.cpp
        src/core/input/mouse_iou.cpp
    )

    # Sirius Joyport (only joyport.cpp)
    add_catch2_test(test_joyport
        tests/unit/test_joyport.cpp
        src/core/input/joyport.cpp
        src/core/input/mouse_iou.cpp
    )

    # AY-3-8910 (only ay8910.cpp)
    add_catch2_test(test_ay8910
        tests/unit/test_ay8910.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
    )

    # VIA 6522 (via6522.cpp + ay8910.cpp since VIA connects to PSG)
    add_catch2_test(test_via6522
        tests/unit/test_via6522.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
    )

    # Block device (only block_device.cpp)
    add_catch2_test(test_block_device
        tests/unit/test_block_device.cpp
        src/core/cards/smartport/block_device.cpp
    )

    # BASIC detokenizer (only basic_detokenizer.cpp + common helpers)
    add_catch2_test(test_basic_detokenizer
        tests/unit/test_basic_detokenizer.cpp
        src/core/basic/basic_detokenizer.cpp
        ${TEST_COMMON_SOURCES}
    )

    # BASIC tokenizer (basic_tokenizer.cpp + basic_detokenizer.cpp for round-trip)
    add_catch2_test(test_basic_tokenizer
        tests/unit/test_basic_tokenizer.cpp
        src/core/basic/basic_tokenizer.cpp
        src/core/basic/basic_detokenizer.cpp
        ${TEST_COMMON_SOURCES}
    )

    # ==========================================================================
    # Catch2 Unit Tests - ROM-dependent
    # ==========================================================================

    # MMU (mmu.cpp + noslot_clock.cpp)
    add_catch2_test(test_mmu
        tests/unit/test_mmu.cpp
        src/core/mmu/mmu.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # MMU Slots (mmu.cpp + thunderclock for testing)
    add_catch2_test(test_mmu_slots
        tests/unit/test_mmu_slots.cpp
        src/core/mmu/mmu.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
    )

    # Machine profile. Links the whole core: the point of these tests is that a
    # profile reaches the subsystems that read it, including the Emulator that
    # selects the CPU and the ROMs.
    add_catch2_test(test_machine_profile
        tests/unit/test_machine_profile.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # NTSC composite decoding (standalone — no MMU needed)
    add_catch2_test(test_ntsc
        tests/unit/test_ntsc.cpp
        src/core/video/ntsc.cpp
    )

    # Video (video.cpp + mmu.cpp)
    add_catch2_test(test_video
        tests/unit/test_video.cpp
        src/core/video/video.cpp
        src/core/video/ntsc.cpp
        src/core/mmu/mmu.cpp
        src/core/input/mouse_iou.cpp
        src/core/noslot_clock.cpp
    )

    # Disk2 Card (disk2_card.cpp + disk images + gcr)
    add_catch2_test(test_disk2_card
        tests/unit/test_disk2_card.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/disk2/disk2_card.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # A //c's serial ports — the SSC's ACIA with no card around it
    add_catch2_test(test_serial_port
        tests/unit/test_serial_port.cpp
        src/core/cards/serial/serial_port.cpp
        src/core/cards/ssc/acia6551.cpp
    )

    # IWM — a //c's drive controller, sharing DiskController with the card
    add_catch2_test(test_iwm
        tests/unit/test_iwm.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/disk2/disk2_card.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # DSK disk image
    add_catch2_test(test_dsk_disk_image
        tests/unit/test_dsk_disk_image.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # WOZ disk image
    add_catch2_test(test_woz_disk_image
        tests/unit/test_woz_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # Disk image format conversion (DSK/DO/PO/WOZ)
    add_catch2_test(test_disk_converter
        tests/unit/test_disk_converter.cpp
        src/core/disk-image/disk_converter.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
        src/core/filesystem/dos33.cpp
        src/core/filesystem/prodos.cpp
        ${TEST_COMMON_SOURCES}
    )

    # Mockingboard (mockingboard_card.cpp + via + ay)
    add_catch2_test(test_mockingboard
        tests/unit/test_mockingboard.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
    )

    # Thunderclock (Catch2 version)
    add_catch2_test(test_thunderclock
        tests/unit/test_thunderclock.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
    )

    # Mouse Card (mouse_card.cpp)
    add_catch2_test(test_mouse_card
        tests/unit/test_mouse_card.cpp
        src/core/cards/mouse/mouse_card.cpp
    )

    # SmartPort Card (smartport + block_device)
    add_catch2_test(test_smartport_card
        tests/unit/test_smartport_card.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
    )

    # ==========================================================================
    # Catch2 Unit Tests - Filesystem
    # ==========================================================================

    # DOS 3.3 parser
    add_catch2_test(test_dos33
        tests/unit/test_dos33.cpp
        src/core/filesystem/dos33.cpp
        ${TEST_COMMON_SOURCES}
    )

    # ProDOS parser
    add_catch2_test(test_prodos
        tests/unit/test_prodos.cpp
        src/core/filesystem/prodos.cpp
        ${TEST_COMMON_SOURCES}
    )

    # Pascal parser
    add_catch2_test(test_pascal
        tests/unit/test_pascal.cpp
        src/core/filesystem/pascal.cpp
        ${TEST_COMMON_SOURCES}
    )

    # ==========================================================================
    # Catch2 Unit Tests - Expansion Card Interface
    # ==========================================================================

    add_catch2_test(test_expansion_card
        tests/unit/test_expansion_card.cpp
        src/core/cards/disk_controller.cpp
        src/core/cards/disk2/disk2_card.cpp
        src/core/cards/iwm/iwm.cpp
        src/core/cards/mockingboard/mockingboard_card.cpp
        src/core/cards/thunderclock/thunderclock_card.cpp
        src/core/cards/mouse/mouse_card.cpp
        src/core/cards/smartport/smartport_card.cpp
        src/core/cards/smartport/block_device.cpp
        src/core/cards/mockingboard/via6522.cpp
        src/core/cards/mockingboard/ay8910.cpp
        src/core/debug/debug_log.cpp
        src/core/disk-image/dsk_disk_image.cpp
        src/core/disk-image/woz_disk_image.cpp
        src/core/disk-image/gcr_encoding.cpp
    )

    # ==========================================================================
    # Catch2 Integration Tests (full emulator)
    # ==========================================================================

    add_catch2_test(test_emulator
        tests/integration/test_emulator.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_emulator_debug
        tests/integration/test_emulator_debug.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_emulator_disk
        tests/integration/test_emulator_disk.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_emulator_basic
        tests/integration/test_emulator_basic.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_emulator_state
        tests/integration/test_emulator_state.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    add_catch2_test(test_basic_load_trace
        tests/integration/test_basic_load_trace.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # Super Serial Card
    add_catch2_test(test_ssc
        tests/integration/test_ssc.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # Parallel card PR#1 firmware drive (PROMSEL ACK-modulated ROM addressing)
    add_catch2_test(test_parallel_pr1
        tests/integration/test_parallel_pr1.cpp
        ${CORE_TEST_SOURCES}
        ${TEST_COMMON_SOURCES}
    )

    # Condition evaluator (needs full emulator)
    add_catch2_test(test_condition_evaluator
        tests/unit/test_condition_evaluator.cpp
        ${CORE_TEST_SOURCES}
    )

    # Debug log sink (only debug_log.cpp)
    add_catch2_test(test_debug_log
        tests/unit/test_debug_log.cpp
        src/core/debug/debug_log.cpp
    )

    # Applesoft variable representation (only applesoft_vars.cpp)
    add_catch2_test(test_applesoft_vars
        tests/unit/test_applesoft_vars.cpp
        src/core/basic/applesoft_vars.cpp
    )

endif()
