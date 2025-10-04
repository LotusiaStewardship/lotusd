# Full Turing-Equivalent Script Implementation Guide (Approach A)

## 🎯 Objective

Implement **bounded Turing-equivalent script execution** in Lotus with:
- ✅ Loops, jumps, and function calls
- ✅ Static bounds declaration (no gas fees)
- ✅ Guaranteed termination within declared limits
- ✅ Full programmability for covenants

**Note**: This is technically "bounded Turing-equivalent" not truly "Turing-complete" due to the halting problem, but it's **practically equivalent** for all real-world use cases.

---

## 📋 Part 1: New Opcodes Specification

### 1.1 Bounds Declaration Opcodes

```cpp
// In src/script/script.h, add to opcodetype enum:

// Bounds declaration (multi-byte opcodes using prefix range)
OP_BOUNDS_DECLARE = 0xf0,    // Declare execution bounds header
```

**Script Header Format** (16 bytes):

```
Byte 0:      0xf0              // OP_BOUNDS_DECLARE
Bytes 1-4:   <max_ops>         // uint32_t - Maximum total operations
Bytes 5-6:   <max_loops>       // uint16_t - Maximum loop iterations
Bytes 7-8:   <max_jumps>       // uint16_t - Maximum jumps
Bytes 9-10:  <max_stack>       // uint16_t - Maximum stack depth
Byte 11:     <max_recursion>   // uint8_t - Maximum recursion depth
Bytes 12-13: <max_memory>      // uint16_t - Maximum memory slots
Bytes 14-15: <reserved>        // uint16_t - Reserved for future use
```

### 1.2 Loop Control Opcodes

```cpp
// Loop opcodes
OP_BEGINLOOP = 0xc9,         // <max_count> OP_BEGINLOOP - Start loop
OP_ENDLOOP = 0xca,           // OP_ENDLOOP - End loop (jump back if counter < max)
OP_BREAK = 0xcb,             // OP_BREAK - Exit current loop immediately
OP_CONTINUE = 0xcc,          // OP_CONTINUE - Skip to next iteration
OP_LOOPINDEX = 0xd9,         // OP_LOOPINDEX - Push current loop counter
```

### 1.3 Jump Opcodes

```cpp
// Jump opcodes
OP_JUMP = 0xcd,              // <offset> OP_JUMP - Unconditional jump
OP_JUMPI = 0xce,             // <offset> <condition> OP_JUMPI - Conditional jump
OP_LABEL = 0xcf,             // <label_id> OP_LABEL - Define jump target
OP_JUMPTO = 0xd0,            // <label_id> OP_JUMPTO - Jump to label
```

### 1.4 Function/Subroutine Opcodes

```cpp
// Function opcodes
OP_CALL = 0xd1,              // <offset> OP_CALL - Call subroutine
OP_CALLTO = 0xd2,            // <label_id> OP_CALLTO - Call subroutine by label
OP_RETURN = 0xd3,            // OP_RETURN - Return from subroutine
OP_CALLDEPTH = 0xd4,         // OP_CALLDEPTH - Push current call depth
```

### 1.5 Memory Opcodes

```cpp
// Memory opcodes (scratch space during execution)
OP_MSTORE = 0xd5,            // <key> <value> OP_MSTORE - Store to memory
OP_MLOAD = 0xd6,             // <key> OP_MLOAD - Load from memory
OP_MSIZE = 0xd7,             // OP_MSIZE - Push number of memory slots used
OP_MCLEAR = 0xd8,            // <key> OP_MCLEAR - Clear memory slot
```

---

## 📐 Part 2: Data Structures

### 2.1 Script Bounds

**File**: `src/script/script.h`

```cpp
/**
 * Execution bounds declared at start of Turing-equivalent script
 */
struct ScriptBounds {
    uint32_t max_ops;          // Maximum total operations
    uint16_t max_loops;        // Maximum loop iterations (sum of all loops)
    uint16_t max_jumps;        // Maximum jumps executed
    uint16_t max_stack;        // Maximum stack depth
    uint8_t max_recursion;     // Maximum function call depth
    uint16_t max_memory;       // Maximum memory slots
    
    // Global maximums (prevent abuse)
    static const uint32_t GLOBAL_MAX_OPS = 100000;
    static const uint16_t GLOBAL_MAX_LOOPS = 10000;
    static const uint16_t GLOBAL_MAX_JUMPS = 5000;
    static const uint16_t GLOBAL_MAX_STACK = 5000;
    static const uint8_t GLOBAL_MAX_RECURSION = 100;
    static const uint16_t GLOBAL_MAX_MEMORY = 1000;
    
    bool IsValid() const {
        return (max_ops <= GLOBAL_MAX_OPS &&
                max_loops <= GLOBAL_MAX_LOOPS &&
                max_jumps <= GLOBAL_MAX_JUMPS &&
                max_stack <= GLOBAL_MAX_STACK &&
                max_recursion <= GLOBAL_MAX_RECURSION &&
                max_memory <= GLOBAL_MAX_MEMORY);
    }
    
    ScriptBounds() : max_ops(0), max_loops(0), max_jumps(0),
                     max_stack(0), max_recursion(0), max_memory(0) {}
};

/**
 * Parse bounds from script header
 * @return true if valid bounds header found
 */
bool ExtractScriptBounds(const CScript &script, ScriptBounds &bounds);
```

### 2.2 Extended Execution Metrics

**File**: `src/script/interpreter.h`

```cpp
struct ScriptExecutionMetrics {
    // Existing
    uint64_t nSigChecks = 0;
    
    // NEW: Turing-script metrics
    uint32_t nOpsExecuted = 0;        // Total ops executed
    uint16_t nLoopIterations = 0;     // Total loop iterations
    uint16_t nJumpsExecuted = 0;      // Total jumps
    uint16_t nMaxStackDepth = 0;      // Maximum stack depth reached
    uint8_t nRecursionDepth = 0;      // Current call depth
    uint16_t nMemoryUsed = 0;         // Memory slots allocated
    
    void IncrementOp() { nOpsExecuted++; }
    void IncrementLoop() { nLoopIterations++; }
    void IncrementJump() { nJumpsExecuted++; }
    void UpdateStackDepth(size_t depth) {
        nMaxStackDepth = std::max(nMaxStackDepth, (uint16_t)depth);
    }
    void IncrementRecursion() { nRecursionDepth++; }
    void DecrementRecursion() { nRecursionDepth--; }
    void UpdateMemory(size_t size) {
        nMemoryUsed = std::max(nMemoryUsed, (uint16_t)size);
    }
    
    bool ExceedsBounds(const ScriptBounds &bounds) const {
        return (nOpsExecuted > bounds.max_ops ||
                nLoopIterations > bounds.max_loops ||
                nJumpsExecuted > bounds.max_jumps ||
                nMaxStackDepth > bounds.max_stack ||
                nRecursionDepth > bounds.max_recursion ||
                nMemoryUsed > bounds.max_memory);
    }
};
```

### 2.3 Execution Context Stacks

**File**: `src/script/interpreter.cpp` (in EvalScript function)

```cpp
// Add these structures to EvalScript function:

struct LoopContext {
    CScript::const_iterator startPos;  // Loop start position
    int64_t counter;                   // Current iteration
    int64_t maxIterations;             // Maximum iterations
};
std::vector<LoopContext> loopStack;

struct CallContext {
    CScript::const_iterator returnPos; // Return address
    size_t stackSize;                  // Stack size at call time
};
std::vector<CallContext> callStack;

struct LabelInfo {
    uint16_t labelId;
    CScript::const_iterator position;
};
std::map<uint16_t, CScript::const_iterator> labelMap;

// Scratch memory (cleared after script execution)
std::map<valtype, valtype> scriptMemory;
```

---

## 🔨 Part 3: Implementation

### 3.1 Bounds Extraction

**File**: `src/script/script.cpp`

```cpp
bool ExtractScriptBounds(const CScript &script, ScriptBounds &bounds) {
    // Check minimum size for bounds header
    if (script.size() < 16) {
        return false;
    }
    
    // Check for bounds declaration opcode
    if (script[0] != OP_BOUNDS_DECLARE) {
        // Script doesn't declare bounds - use defaults
        bounds = ScriptBounds();
        return true;  // Valid (uses default limits)
    }
    
    // Parse bounds
    bounds.max_ops = ReadLE32(&script[1]);
    bounds.max_loops = ReadLE16(&script[5]);
    bounds.max_jumps = ReadLE16(&script[7]);
    bounds.max_stack = ReadLE16(&script[9]);
    bounds.max_recursion = script[11];
    bounds.max_memory = ReadLE16(&script[12]);
    
    // Validate against global maximums
    if (!bounds.IsValid()) {
        return false;
    }
    
    return true;
}
```

### 3.2 Pre-Execution Validation

**File**: `src/script/interpreter.cpp`

```cpp
bool PreValidateScript(const CScript &script, const ScriptBounds &bounds) {
    // 1. Build label map (find all OP_LABEL positions)
    std::map<uint16_t, CScript::const_iterator> labelMap;
    
    CScript::const_iterator pc = script.begin();
    if (script[0] == OP_BOUNDS_DECLARE) {
        pc += 16;  // Skip bounds header
    }
    
    while (pc < script.end()) {
        opcodetype opcode;
        valtype data;
        
        if (!script.GetOp(pc, opcode, data)) {
            return false;
        }
        
        if (opcode == OP_LABEL) {
            if (data.size() != 2) {
                return false;  // Label ID must be 2 bytes
            }
            uint16_t labelId = ReadLE16(&data[0]);
            
            if (labelMap.count(labelId)) {
                return false;  // Duplicate label
            }
            
            labelMap[labelId] = pc;
        }
    }
    
    // 2. Validate all jumps have valid targets
    pc = script.begin();
    if (script[0] == OP_BOUNDS_DECLARE) {
        pc += 16;
    }
    
    while (pc < script.end()) {
        opcodetype opcode;
        valtype data;
        
        if (!script.GetOp(pc, opcode, data)) {
            return false;
        }
        
        if (opcode == OP_JUMPTO || opcode == OP_CALLTO) {
            if (data.size() != 2) {
                return false;
            }
            uint16_t labelId = ReadLE16(&data[0]);
            
            if (!labelMap.count(labelId)) {
                return false;  // Jump to undefined label
            }
        }
    }
    
    return true;
}
```

### 3.3 OP_BEGINLOOP Implementation

**File**: `src/script/interpreter.cpp`

```cpp
case OP_BEGINLOOP: {
    // Stack: <max_iterations>
    if (stack.size() < 1) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    CScriptNum maxIter(stacktop(-1), fRequireMinimal);
    popstack(stack);
    
    // Validate iteration count is reasonable
    if (maxIter.getint() < 0 || maxIter.getint() > 10000) {
        return set_error(serror, ScriptError::LOOP_COUNT_INVALID);
    }
    
    // Check against declared bounds
    int64_t potentialTotal = metrics.nLoopIterations + maxIter.getint();
    if (potentialTotal > scriptBounds.max_loops) {
        return set_error(serror, ScriptError::LOOP_LIMIT_EXCEEDED);
    }
    
    // Push loop context
    LoopContext ctx;
    ctx.startPos = pc;
    ctx.counter = 0;
    ctx.maxIterations = maxIter.getint();
    loopStack.push_back(ctx);
} break;
```

### 3.4 OP_ENDLOOP Implementation

```cpp
case OP_ENDLOOP: {
    if (loopStack.empty()) {
        return set_error(serror, ScriptError::LOOP_NOT_STARTED);
    }
    
    LoopContext &loop = loopStack.back();
    loop.counter++;
    metrics.IncrementLoop();
    
    // Check if we should continue looping
    if (loop.counter < loop.maxIterations) {
        // Jump back to loop start
        pc = loop.startPos;
        metrics.IncrementJump();
        
        // Verify jump limit
        if (metrics.ExceedsBounds(scriptBounds)) {
            return set_error(serror, ScriptError::JUMP_LIMIT_EXCEEDED);
        }
    } else {
        // Loop complete, exit
        loopStack.pop_back();
    }
} break;
```

### 3.5 OP_BREAK Implementation

```cpp
case OP_BREAK: {
    if (loopStack.empty()) {
        return set_error(serror, ScriptError::BREAK_OUTSIDE_LOOP);
    }
    
    // Find matching OP_ENDLOOP and jump past it
    loopStack.pop_back();
    
    int depth = 1;  // Nested loop depth
    while (pc < pend && depth > 0) {
        opcodetype op;
        valtype data;
        
        if (!script.GetOp(pc, op, data)) {
            return set_error(serror, ScriptError::BAD_OPCODE);
        }
        
        if (op == OP_BEGINLOOP) depth++;
        if (op == OP_ENDLOOP) depth--;
    }
    
    if (depth != 0) {
        return set_error(serror, ScriptError::UNMATCHED_LOOP);
    }
    
    metrics.IncrementJump();
} break;
```

### 3.6 OP_CONTINUE Implementation

```cpp
case OP_CONTINUE: {
    if (loopStack.empty()) {
        return set_error(serror, ScriptError::CONTINUE_OUTSIDE_LOOP);
    }
    
    // Jump directly to OP_ENDLOOP (which will handle loop logic)
    int depth = 1;
    while (pc < pend && depth > 0) {
        opcodetype op;
        valtype data;
        
        if (!script.GetOp(pc, op, data)) {
            return set_error(serror, ScriptError::BAD_OPCODE);
        }
        
        if (op == OP_BEGINLOOP) depth++;
        if (op == OP_ENDLOOP) {
            depth--;
            if (depth == 0) {
                // Found our OP_ENDLOOP, position before it
                pc--;  // Step back so OP_ENDLOOP executes next
                break;
            }
        }
    }
    
    metrics.IncrementJump();
} break;
```

### 3.7 OP_LOOPINDEX Implementation

```cpp
case OP_LOOPINDEX: {
    if (loopStack.empty()) {
        return set_error(serror, ScriptError::NOT_IN_LOOP);
    }
    
    // Push current loop counter to stack
    const LoopContext &loop = loopStack.back();
    CScriptNum counter(loop.counter);
    stack.push_back(counter.getvch());
} break;
```

### 3.8 OP_LABEL Implementation

```cpp
case OP_LABEL: {
    // Labels are passive markers - no runtime action
    // They're processed during pre-validation
    // At runtime, just skip them
    
    // Note: PC already advanced past the label data
} break;
```

### 3.9 OP_JUMP & OP_JUMPI Implementation

```cpp
case OP_JUMP: {
    // Stack: <offset>
    if (stack.size() < 1) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    CScriptNum offset(stacktop(-1), fRequireMinimal);
    popstack(stack);
    
    // Validate offset
    CScript::const_iterator newPos = pc + offset.getint();
    if (newPos < script.begin() || newPos >= pend) {
        return set_error(serror, ScriptError::INVALID_JUMP_TARGET);
    }
    
    pc = newPos;
    metrics.IncrementJump();
    
    if (metrics.ExceedsBounds(scriptBounds)) {
        return set_error(serror, ScriptError::JUMP_LIMIT_EXCEEDED);
    }
} break;

case OP_JUMPI: {
    // Stack: <offset> <condition>
    if (stack.size() < 2) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    bool condition = CastToBool(stacktop(-1));
    CScriptNum offset(stacktop(-2), fRequireMinimal);
    popstack(stack);
    popstack(stack);
    
    if (condition) {
        // Validate offset
        CScript::const_iterator newPos = pc + offset.getint();
        if (newPos < script.begin() || newPos >= pend) {
            return set_error(serror, ScriptError::INVALID_JUMP_TARGET);
        }
        
        pc = newPos;
        metrics.IncrementJump();
        
        if (metrics.ExceedsBounds(scriptBounds)) {
            return set_error(serror, ScriptError::JUMP_LIMIT_EXCEEDED);
        }
    }
} break;
```

### 3.10 OP_JUMPTO Implementation

```cpp
case OP_JUMPTO: {
    // Stack: <label_id>
    if (stack.size() < 1) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    CScriptNum labelId(stacktop(-1), fRequireMinimal);
    popstack(stack);
    
    // Look up label in pre-built map
    uint16_t label = labelId.getint();
    if (!labelMap.count(label)) {
        return set_error(serror, ScriptError::UNDEFINED_LABEL);
    }
    
    pc = labelMap[label];
    metrics.IncrementJump();
    
    if (metrics.ExceedsBounds(scriptBounds)) {
        return set_error(serror, ScriptError::JUMP_LIMIT_EXCEEDED);
    }
} break;
```

### 3.11 OP_CALL & OP_RETURN Implementation

```cpp
case OP_CALL: {
    // Stack: <offset>
    if (stack.size() < 1) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    CScriptNum offset(stacktop(-1), fRequireMinimal);
    popstack(stack);
    
    // Check recursion limit
    metrics.IncrementRecursion();
    if (metrics.nRecursionDepth > scriptBounds.max_recursion) {
        return set_error(serror, ScriptError::RECURSION_LIMIT_EXCEEDED);
    }
    
    // Push return address to call stack
    CallContext ctx;
    ctx.returnPos = pc;
    ctx.stackSize = stack.size();
    callStack.push_back(ctx);
    
    // Jump to function
    CScript::const_iterator newPos = script.begin() + offset.getint();
    if (newPos < script.begin() || newPos >= pend) {
        return set_error(serror, ScriptError::INVALID_CALL_TARGET);
    }
    
    pc = newPos;
    metrics.IncrementJump();
} break;

case OP_CALLTO: {
    // Stack: <label_id>
    if (stack.size() < 1) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    CScriptNum labelId(stacktop(-1), fRequireMinimal);
    popstack(stack);
    
    // Check recursion limit
    metrics.IncrementRecursion();
    if (metrics.nRecursionDepth > scriptBounds.max_recursion) {
        return set_error(serror, ScriptError::RECURSION_LIMIT_EXCEEDED);
    }
    
    // Look up label
    uint16_t label = labelId.getint();
    if (!labelMap.count(label)) {
        return set_error(serror, ScriptError::UNDEFINED_LABEL);
    }
    
    // Push return address
    CallContext ctx;
    ctx.returnPos = pc;
    ctx.stackSize = stack.size();
    callStack.push_back(ctx);
    
    // Jump to function
    pc = labelMap[label];
    metrics.IncrementJump();
} break;

case OP_RETURN: {
    if (callStack.empty()) {
        return set_error(serror, ScriptError::RETURN_OUTSIDE_FUNCTION);
    }
    
    // Pop return address
    CallContext ctx = callStack.back();
    callStack.pop_back();
    
    // Jump back to caller
    pc = ctx.returnPos;
    
    metrics.DecrementRecursion();
    metrics.IncrementJump();
} break;
```

### 3.12 Memory Operations

```cpp
case OP_MSTORE: {
    // Stack: <key> <value>
    if (stack.size() < 2) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    valtype key = stacktop(-2);
    valtype value = stacktop(-1);
    popstack(stack);
    popstack(stack);
    
    // Store in memory
    scriptMemory[key] = value;
    
    // Check memory limit
    metrics.UpdateMemory(scriptMemory.size());
    if (metrics.nMemoryUsed > scriptBounds.max_memory) {
        return set_error(serror, ScriptError::MEMORY_LIMIT_EXCEEDED);
    }
} break;

case OP_MLOAD: {
    // Stack: <key>
    if (stack.size() < 1) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    valtype key = stacktop(-1);
    popstack(stack);
    
    // Load from memory (push empty if not found)
    if (scriptMemory.count(key)) {
        stack.push_back(scriptMemory[key]);
    } else {
        stack.push_back(valtype());  // Empty value
    }
} break;

case OP_MSIZE: {
    CScriptNum size(scriptMemory.size());
    stack.push_back(size.getvch());
} break;

case OP_MCLEAR: {
    // Stack: <key>
    if (stack.size() < 1) {
        return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
    }
    
    valtype key = stacktop(-1);
    popstack(stack);
    
    scriptMemory.erase(key);
} break;
```

### 3.13 Modified EvalScript Main Loop

**File**: `src/script/interpreter.cpp`

```cpp
bool EvalScript(std::vector<valtype> &stack, const CScript &script,
                uint32_t flags, const BaseSignatureChecker &checker,
                ScriptExecutionMetrics &metrics, ScriptExecutionData &execdata,
                ScriptError *serror) {
    // ... existing setup code ...
    
    // NEW: Parse and validate bounds
    ScriptBounds scriptBounds;
    if (!ExtractScriptBounds(script, scriptBounds)) {
        return set_error(serror, ScriptError::INVALID_BOUNDS);
    }
    
    // NEW: Pre-validate script (build label map, validate jumps)
    std::map<uint16_t, CScript::const_iterator> labelMap;
    if (!PreValidateScript(script, scriptBounds, labelMap)) {
        return set_error(serror, ScriptError::INVALID_SCRIPT_STRUCTURE);
    }
    
    // NEW: Execution context
    std::vector<LoopContext> loopStack;
    std::vector<CallContext> callStack;
    std::map<valtype, valtype> scriptMemory;
    
    // Adjust starting position if bounds header present
    CScript::const_iterator pc = script.begin();
    if (script[0] == OP_BOUNDS_DECLARE) {
        pc += 16;  // Skip bounds header
    }
    
    // Main evaluation loop
    for (; pc < pend; ++opcode_pos) {
        // ... existing code ...
        
        // NEW: Check bounds after each operation
        metrics.IncrementOp();
        metrics.UpdateStackDepth(stack.size() + altstack.size());
        
        if (metrics.ExceedsBounds(scriptBounds)) {
            return set_error(serror, ScriptError::BOUNDS_EXCEEDED);
        }
        
        // ... rest of existing evaluation ...
    }
    
    // NEW: Verify all loops/calls properly closed
    if (!loopStack.empty()) {
        return set_error(serror, ScriptError::UNCLOSED_LOOP);
    }
    
    if (!callStack.empty()) {
        return set_error(serror, ScriptError::UNCLOSED_FUNCTION);
    }
    
    return set_success(serror);
}
```

---

## 🎨 Part 4: Complete Examples

### Example 1: Simple Counter Loop

```
// Header: max 100 ops, 10 loops
f0                              // OP_BOUNDS_DECLARE
64 00 00 00                     // max_ops = 100
0a 00                           // max_loops = 10  
05 00                           // max_jumps = 5
64 00                           // max_stack = 100
05                              // max_recursion = 5
0a 00                           // max_memory = 10
00 00                           // reserved

// Code: Loop 5 times, sum counter
00                              // sum = 0
05                              // max_iterations = 5
c9                              // OP_BEGINLOOP
  d9                            // OP_LOOPINDEX (push counter)
  93                            // OP_ADD (add to sum)
ca                              // OP_ENDLOOP

// Result: sum = 0+1+2+3+4 = 10
```

### Example 2: Validate All Outputs Have Same Genesis

```
// Bounds header
f0 e8 03 00 00  14 00  0a 00  c8 00  01  14 00  00 00

// Genesis ID to match
20 <genesis_32_bytes>

// Initialize
00                              // counter = 0
c3                              // OP_TXOUTPUTCOUNT (max)

// Loop through all outputs
c9                              // OP_BEGINLOOP
  76                            // OP_DUP (dup counter)
  c8                            // OP_OUTPUTBYTECODE (get output script)
  
  // Extract first 32 bytes (genesis of output)
  00 20                         // offset=0, length=32
  7f                            // OP_SPLIT
  75                            // OP_DROP (drop rest)
  
  // Compare with our genesis
  20 <genesis_32_bytes>
  87                            // OP_EQUAL
  
  69                            // OP_VERIFY (must match!)
  
  51 93                         // OP_1 OP_ADD (counter++)
ca                              // OP_ENDLOOP

75                              // OP_DROP (clean up counter)

// All outputs validated!
// Standard P2PKH signature check
76 a9 14 <pkh_20_bytes> 88 ac
```

### Example 3: Fibonacci with Loop

```
// Calculate fibonacci(10)
f0 e8 03 00 00  64 00  14 00  c8 00  01  00 00  00 00

// Initialize: fib(0)=0, fib(1)=1
00                              // fib(n-2)
01                              // fib(n-1)

// Loop 10 times
0a
c9                              // OP_BEGINLOOP
  72                            // OP_2DUP (copy both)
  93                            // OP_ADD (fib(n) = fib(n-1) + fib(n-2))
  7b                            // OP_ROT (bring old fib(n-2) to top)
  75                            // OP_DROP (remove it)
ca                              // OP_ENDLOOP

// Stack now has fib(10) = 55
```

### Example 4: Function Call

```
// Bounds header
f0 e8 03 00 00  64 00  1e 00  c8 00  0a  0a 00  00 00

// Main code
05                              // Argument
cf 01 00                        // OP_LABEL 1 (main entry)
  cf 0a 00                      // <label_10>
  d2                            // OP_CALLTO (call function at label 10)
  // Function returned, result on stack
  cd 14 00                      // OP_JUMP +20 (skip function)

// Function definition
cf 0a 00                        // OP_LABEL 10 (function start)
  76                            // OP_DUP
  51 93                         // OP_1 OP_ADD (increment)
  d3                            // OP_RETURN

// End of script
76 a9 14 <pkh> 88 ac           // P2PKH sig check
```

### Example 5: Merkle Proof Verification Loop

```
// Verify element is in Merkle tree
f0 10 27 00 00  20 00  10 00  64 00  01  00 00  00 00

// Stack setup: <merkle_root> <leaf_hash> <proof_sibling_count>

// Loop through proof siblings
c9                              // OP_BEGINLOOP
  // Load next sibling from script data
  <sibling_32_bytes>
  
  // Concatenate in sorted order (smaller hash first)
  72                            // OP_2DUP
  a0                            // OP_GREATERTHAN
  63                            // OP_IF
    7c                          // OP_SWAP
  68                            // OP_ENDIF
  
  7e                            // OP_CAT (concatenate)
  a8                            // OP_SHA256 (hash)
ca                              // OP_ENDLOOP

// Result should equal merkle root
87 69                           // OP_EQUAL OP_VERIFY

// Signature check
76 a9 14 <pkh> 88 ac
```

---

## 🛡️ Part 5: Security & Validation

### 5.1 Static Analysis Engine

**File**: `src/script/validation.h` (NEW)

```cpp
/**
 * Static analysis results
 */
struct StaticAnalysis {
    uint32_t worst_case_ops;
    uint16_t worst_case_loops;
    uint16_t worst_case_jumps;
    uint16_t worst_case_stack;
    uint8_t worst_case_recursion;
    
    std::set<uint16_t> labels;
    std::set<CScript::const_iterator> jump_targets;
    
    bool has_unbounded_construct;
    bool has_invalid_jump;
    
    std::string error_message;
};

/**
 * Perform static analysis on script
 * Returns false if script is provably invalid
 */
bool AnalyzeScript(const CScript &script, StaticAnalysis &result);
```

**File**: `src/script/validation.cpp` (NEW)

```cpp
bool AnalyzeScript(const CScript &script, StaticAnalysis &result) {
    result = StaticAnalysis();
    
    // Skip bounds header if present
    CScript::const_iterator pc = script.begin();
    if (script[0] == OP_BOUNDS_DECLARE) {
        pc += 16;
    }
    
    int current_stack_depth = 0;
    int loop_depth = 0;
    
    // First pass: Find all labels
    CScript::const_iterator scan_pc = pc;
    while (scan_pc < script.end()) {
        opcodetype opcode;
        valtype data;
        
        if (!script.GetOp(scan_pc, opcode, data)) {
            result.error_message = "Malformed script";
            return false;
        }
        
        if (opcode == OP_LABEL) {
            if (data.size() != 2) {
                result.error_message = "Invalid label size";
                return false;
            }
            uint16_t label_id = ReadLE16(&data[0]);
            result.labels.insert(label_id);
        }
    }
    
    // Second pass: Analyze control flow
    while (pc < script.end()) {
        opcodetype opcode;
        valtype data;
        
        if (!script.GetOp(pc, opcode, data)) {
            return false;
        }
        
        result.worst_case_ops++;
        
        // Track loops
        if (opcode == OP_BEGINLOOP) {
            loop_depth++;
            // Assume worst case: maximum iterations
            result.worst_case_loops += 10000;  // Conservative estimate
        }
        
        if (opcode == OP_ENDLOOP) {
            loop_depth--;
            if (loop_depth < 0) {
                result.error_message = "ENDLOOP without BEGINLOOP";
                return false;
            }
        }
        
        // Track jumps
        if (opcode == OP_JUMP || opcode == OP_JUMPI) {
            result.worst_case_jumps++;
        }
        
        if (opcode == OP_JUMPTO || opcode == OP_CALLTO) {
            result.worst_case_jumps++;
            
            // Verify label exists
            if (data.size() != 2) {
                result.error_message = "Invalid label reference";
                return false;
            }
            uint16_t label = ReadLE16(&data[0]);
            if (!result.labels.count(label)) {
                result.error_message = "Jump to undefined label";
                result.has_invalid_jump = true;
                return false;
            }
        }
        
        // Track calls
        if (opcode == OP_CALL || opcode == OP_CALLTO) {
            result.worst_case_recursion++;
        }
        
        // Track stack depth (simplified)
        int stack_effect = GetStackEffect(opcode);
        current_stack_depth += stack_effect;
        result.worst_case_stack = std::max(
            result.worst_case_stack,
            (uint16_t)current_stack_depth
        );
    }
    
    // Verify all loops closed
    if (loop_depth != 0) {
        result.error_message = "Unclosed loop";
        return false;
    }
    
    return true;
}

int GetStackEffect(opcodetype opcode) {
    switch (opcode) {
        // Push opcodes: +1
        case OP_0: case OP_1: case OP_2: case OP_3: case OP_4:
        case OP_5: case OP_6: case OP_7: case OP_8: case OP_9:
        case OP_10: case OP_11: case OP_12: case OP_13: case OP_14:
        case OP_15: case OP_16:
        case OP_TXVERSION: case OP_TXINPUTCOUNT: case OP_TXOUTPUTCOUNT:
        case OP_TXLOCKTIME: case OP_UTXOVALUE: case OP_INPUTINDEX:
        case OP_ACTIVEBYTECODE: case OP_LOOPINDEX: case OP_MSIZE:
            return +1;
        
        // Dup opcodes: +1
        case OP_DUP: case OP_OVER:
            return +1;
        
        // Drop opcodes: -1
        case OP_DROP: case OP_1ADD: case OP_1SUB:
            return -1;
        
        // Two-input ops: -1 (consume 2, produce 1)
        case OP_ADD: case OP_SUB: case OP_CAT: case OP_EQUAL:
        case OP_LESSTHAN: case OP_GREATERTHAN:
            return -1;
        
        // Default: 0 (conservative)
        default:
            return 0;
    }
}
```

### 5.2 Consensus Integration

**File**: `src/validation.cpp`

```cpp
// Add to CheckInputScripts, after covenant balance validation:

// Validate Turing-script bounds
for (size_t i = 0; i < tx.vin.size(); i++) {
    const Coin &coin = inputs.AccessCoin(tx.vin[i].prevout);
    const CScript &scriptPubKey = coin.GetTxOut().scriptPubKey;
    
    // Check if this is a Turing-script
    if (scriptPubKey.size() > 0 && scriptPubKey[0] == OP_BOUNDS_DECLARE) {
        ScriptBounds bounds;
        if (!ExtractScriptBounds(scriptPubKey, bounds)) {
            return state.Invalid(
                TxValidationResult::TX_CONSENSUS,
                "invalid-script-bounds",
                "Script bounds declaration is invalid");
        }
        
        // Perform static analysis
        StaticAnalysis analysis;
        if (!AnalyzeScript(scriptPubKey, analysis)) {
            return state.Invalid(
                TxValidationResult::TX_CONSENSUS,
                "script-static-analysis-failed",
                analysis.error_message);
        }
        
        // Verify declared bounds are sufficient for worst-case
        if (analysis.worst_case_ops > bounds.max_ops) {
            return state.Invalid(
                TxValidationResult::TX_CONSENSUS,
                "bounds-insufficient",
                "Declared max_ops insufficient for worst case");
        }
    }
}
```

### 5.3 New Script Errors

**File**: `src/script/script_error.h`

```cpp
enum class ScriptError {
    // ... existing errors ...
    
    // Turing-script errors
    INVALID_BOUNDS,
    BOUNDS_EXCEEDED,
    LOOP_NOT_STARTED,
    LOOP_COUNT_INVALID,
    LOOP_LIMIT_EXCEEDED,
    UNCLOSED_LOOP,
    BREAK_OUTSIDE_LOOP,
    CONTINUE_OUTSIDE_LOOP,
    NOT_IN_LOOP,
    JUMP_LIMIT_EXCEEDED,
    INVALID_JUMP_TARGET,
    UNDEFINED_LABEL,
    INVALID_SCRIPT_STRUCTURE,
    RETURN_OUTSIDE_FUNCTION,
    UNCLOSED_FUNCTION,
    RECURSION_LIMIT_EXCEEDED,
    MEMORY_LIMIT_EXCEEDED,
    INVALID_CALL_TARGET,
};
```

---

## 💰 Part 6: Fee Model (No Gas!)

### 6.1 Cost Estimation

**File**: `src/policy/fees.h`

```cpp
/**
 * Calculate validation cost from declared bounds
 * Used for fee estimation, NOT runtime metering
 */
class ScriptCostEstimator {
public:
    static uint64_t EstimateCost(const ScriptBounds &bounds) {
        uint64_t cost = 0;
        
        // Base operations: 10 CPU cycles per op
        cost += bounds.max_ops * 10;
        
        // Loops: 50 cycles overhead per iteration
        cost += bounds.max_loops * 50;
        
        // Jumps: 20 cycles per jump
        cost += bounds.max_jumps * 20;
        
        // Stack: 2 cycles per depth level
        cost += bounds.max_stack * 2;
        
        // Recursion: 100 cycles per level
        cost += bounds.max_recursion * 100;
        
        // Memory: 10 cycles per slot
        cost += bounds.max_memory * 10;
        
        return cost;
    }
    
    static Amount EstimateFee(const ScriptBounds &bounds) {
        uint64_t cost = EstimateCost(bounds);
        
        // Fee formula: base + (cost / 1000) satoshis
        // Example: cost=10000 → fee=1010 sats
        Amount baseFee = 1000 * SATOSHI;
        Amount costFee = (cost / 1000) * SATOSHI;
        
        return baseFee + costFee;
    }
    
    static bool IsReasonable(const ScriptBounds &bounds) {
        // Reject if estimated cost > 10ms on modern CPU
        uint64_t cost = EstimateCost(bounds);
        const uint64_t MAX_COST = 10000000;  // 10M cycles ~= 10ms
        
        return cost <= MAX_COST;
    }
};
```

### 6.2 Miner Policy

**File**: `src/policy/policy.cpp`

```cpp
bool IsStandardTuringScript(const CScript &script, std::string &reason) {
    ScriptBounds bounds;
    if (!ExtractScriptBounds(script, bounds)) {
        reason = "invalid-bounds-header";
        return false;
    }
    
    // Verify bounds are within global limits
    if (!bounds.IsValid()) {
        reason = "bounds-exceed-global-maximum";
        return false;
    }
    
    // Miners can reject if cost too high
    if (!ScriptCostEstimator::IsReasonable(bounds)) {
        reason = "script-too-expensive";
        return false;
    }
    
    // Perform static analysis
    StaticAnalysis analysis;
    if (!AnalyzeScript(script, analysis)) {
        reason = "static-analysis-failed: " + analysis.error_message;
        return false;
    }
    
    return true;
}
```

---

## 🧪 Part 7: Testing

### 7.1 Unit Tests

**File**: `src/test/script_turing_tests.cpp` (NEW)

```cpp
#include <boost/test/unit_test.hpp>
#include <script/interpreter.h>
#include <script/script.h>

BOOST_AUTO_TEST_SUITE(script_turing_tests)

BOOST_AUTO_TEST_CASE(simple_loop) {
    // Loop 5 times, accumulate counter
    CScript script;
    script << OP_BOUNDS_DECLARE
           << uint32_t(100) << uint16_t(10) << uint16_t(5)
           << uint16_t(100) << uint8_t(5) << uint16_t(10) << uint16_t(0)
           << 0 << 5 << OP_BEGINLOOP
           << OP_LOOPINDEX << OP_ADD
           << OP_ENDLOOP;
    
    std::vector<valtype> stack;
    ScriptExecutionMetrics metrics;
    ScriptExecutionData execdata{script};
    ScriptError err;
    
    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_NONE,
                          BaseSignatureChecker(), metrics, execdata, &err));
    BOOST_CHECK_EQUAL(stack.size(), 1);
    
    // Result should be 0+1+2+3+4 = 10
    CScriptNum result(stack[0], false);
    BOOST_CHECK_EQUAL(result.getint(), 10);
}

BOOST_AUTO_TEST_CASE(nested_loops) {
    // Outer: 3 iterations, Inner: 2 iterations = 6 total
    CScript script;
    script << OP_BOUNDS_DECLARE
           << uint32_t(200) << uint16_t(20) << uint16_t(10)
           << uint16_t(100) << uint8_t(5) << uint16_t(10) << uint16_t(0)
           << 0                         // sum = 0
           << 3 << OP_BEGINLOOP         // Outer loop
           << 2 << OP_BEGINLOOP         // Inner loop
           << 1 << OP_ADD               // sum++
           << OP_ENDLOOP
           << OP_ENDLOOP;
    
    std::vector<valtype> stack;
    ScriptExecutionMetrics metrics;
    ScriptExecutionData execdata{script};
    ScriptError err;
    
    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_NONE,
                          BaseSignatureChecker(), metrics, execdata, &err));
    
    CScriptNum result(stack[0], false);
    BOOST_CHECK_EQUAL(result.getint(), 6);  // 3 × 2 = 6
}

BOOST_AUTO_TEST_CASE(loop_with_break) {
    // Loop 100 times, but break at 5
    CScript script;
    script << OP_BOUNDS_DECLARE
           << uint32_t(500) << uint16_t(100) << uint16_t(10)
           << uint16_t(100) << uint8_t(5) << uint16_t(10) << uint16_t(0)
           << 100 << OP_BEGINLOOP
           << OP_LOOPINDEX << 5 << OP_EQUAL
           << OP_IF << OP_BREAK << OP_ENDIF
           << OP_ENDLOOP;
    
    std::vector<valtype> stack;
    ScriptExecutionMetrics metrics;
    ScriptExecutionData execdata{script};
    ScriptError err;
    
    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_NONE,
                          BaseSignatureChecker(), metrics, execdata, &err));
    
    // Should have executed only 5 iterations (not 100)
    BOOST_CHECK_EQUAL(metrics.nLoopIterations, 5);
}

BOOST_AUTO_TEST_CASE(function_call) {
    // Call a function that increments input
    CScript script;
    script << OP_BOUNDS_DECLARE
           << uint32_t(200) << uint16_t(10) << uint16_t(10)
           << uint16_t(100) << uint8_t(5) << uint16_t(10) << uint16_t(0)
           << 5                         // Argument
           << 20 << OP_JUMP             // Jump over function definition
           // Function at offset 20
           << uint16_t(1) << OP_LABEL   // Label 1
           << OP_DUP << 1 << OP_ADD     // x → x+1
           << OP_RETURN
           // Main code continues
           << uint16_t(1) << OP_CALLTO; // Call function
    
    std::vector<valtype> stack;
    ScriptExecutionMetrics metrics;
    ScriptExecutionData execdata{script};
    ScriptError err;
    
    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_NONE,
                          BaseSignatureChecker(), metrics, execdata, &err));
    
    CScriptNum result(stack[0], false);
    BOOST_CHECK_EQUAL(result.getint(), 6);  // 5 + 1 = 6
}

BOOST_AUTO_TEST_CASE(exceed_loop_limit) {
    // Declare max_loops=10 but try to execute 20
    CScript script;
    script << OP_BOUNDS_DECLARE
           << uint32_t(1000) << uint16_t(10) << uint16_t(10)  // max_loops=10
           << uint16_t(100) << uint8_t(5) << uint16_t(10) << uint16_t(0)
           << 20 << OP_BEGINLOOP        // Try 20 iterations
           << OP_ENDLOOP;
    
    std::vector<valtype> stack;
    ScriptExecutionMetrics metrics;
    ScriptExecutionData execdata{script};
    ScriptError err;
    
    // Should FAIL with loop limit exceeded
    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_NONE,
                           BaseSignatureChecker(), metrics, execdata, &err));
    BOOST_CHECK(err == ScriptError::LOOP_LIMIT_EXCEEDED);
}

BOOST_AUTO_TEST_CASE(memory_operations) {
    // Store and load from memory
    CScript script;
    script << OP_BOUNDS_DECLARE
           << uint32_t(100) << uint16_t(10) << uint16_t(5)
           << uint16_t(100) << uint8_t(5) << uint16_t(10) << uint16_t(0)
           << "key" << "value" << OP_MSTORE  // Store
           << "key" << OP_MLOAD               // Load
           << "value" << OP_EQUAL;            // Should match
    
    std::vector<valtype> stack;
    ScriptExecutionMetrics metrics;
    ScriptExecutionData execdata{script};
    ScriptError err;
    
    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_NONE,
                          BaseSignatureChecker(), metrics, execdata, &err));
    BOOST_CHECK_EQUAL(stack.size(), 1);
    BOOST_CHECK(CastToBool(stack[0]));  // Should be true
}

BOOST_AUTO_TEST_SUITE_END()
```

### 7.2 Integration Tests

**File**: `test/functional/feature_turing_script.py` (NEW)

```python
#!/usr/bin/env python3
"""Test Turing-equivalent script execution"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class TuringScriptTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
    
    def run_test(self):
        node = self.nodes[0]
        
        # Generate some blocks
        node.generate(101)
        
        # Test 1: Simple loop script
        self.test_simple_loop(node)
        
        # Test 2: Nested loops
        self.test_nested_loops(node)
        
        # Test 3: Function call
        self.test_function_call(node)
        
        # Test 4: Exceed bounds (should fail)
        self.test_exceed_bounds(node)
        
        # Test 5: Covenant with loop validation
        self.test_covenant_with_loop(node)
    
    def test_simple_loop(self, node):
        """Test basic loop execution"""
        # Script that loops 5 times
        script_hex = "f0..." # Bounds header + loop code
        
        # Decode and verify
        decoded = node.decodescript(script_hex)
        self.log.info(f"Simple loop script: {decoded}")
        
        # Create transaction with this script
        # Verify it validates correctly
        
    def test_exceed_bounds(self, node):
        """Test that exceeding bounds fails"""
        # Script declares max_loops=10 but tries 100
        script_hex = "f0..." # Invalid bounds
        
        # Should be rejected
        assert_raises_rpc_error(-26, "loop-limit-exceeded",
                               node.sendrawtransaction, tx_hex)
```

---

## 📊 Part 8: Performance Analysis

### 8.1 Complexity Analysis

```
Operation          | Time Complexity | Space Complexity
-------------------|-----------------|------------------
Simple op          | O(1)            | O(1)
Loop               | O(n)            | O(1)
Nested loop (d=2)  | O(n²)           | O(1)
Nested loop (d=3)  | O(n³)           | O(1)
Recursion (depth d)| O(2^d)          | O(d)
Jump               | O(1)            | O(1)
Memory access      | O(1)            | O(m)

Where:
  n = max loop iterations
  d = depth (nesting or recursion)
  m = memory slots
```

### 8.2 Worst-Case Validation Time

```cpp
// Calculate worst-case validation time
uint64_t WorstCaseTime(const ScriptBounds &bounds) {
    // Assume modern CPU: 3 GHz = 3 billion cycles/sec
    const double CYCLES_PER_SECOND = 3e9;
    const double CYCLES_PER_OP = 10;
    
    uint64_t total_cycles = 0;
    
    // Operations
    total_cycles += bounds.max_ops * CYCLES_PER_OP;
    
    // Loops (additional overhead)
    total_cycles += bounds.max_loops * 50;
    
    // Jumps
    total_cycles += bounds.max_jumps * 20;
    
    // Memory
    total_cycles += bounds.max_memory * 10;
    
    // Convert to milliseconds
    double ms = (total_cycles / CYCLES_PER_SECOND) * 1000;
    
    return static_cast<uint64_t>(ms);
}

// Example:
// max_ops=10000, max_loops=1000, max_jumps=100, max_memory=100
// → ~170,000 cycles → 0.057 ms on 3GHz CPU
```

### 8.3 Validation Time Limits

```cpp
// In validation.cpp
bool IsScriptTooExpensive(const ScriptBounds &bounds) {
    uint64_t estimated_ms = WorstCaseTime(bounds);
    
    // Miners can configure their own limits
    uint64_t max_allowed_ms = gArgs.GetArg("-maxscripttime", 100);  // 100ms default
    
    if (estimated_ms > max_allowed_ms) {
        LogPrintf("Script rejected: estimated %dms > limit %dms\n",
                  estimated_ms, max_allowed_ms);
        return true;
    }
    
    return false;
}
```

---

## 🎮 Part 9: Advanced Examples

### Example 1: Token Split Validation (Real Covenant)

```
// Validate token split across multiple recipients
// Input: 1000 tokens
// Outputs: Variable number, each with portion of tokens

f0 10 27 00 00  e8 03  64 00  c8 00  05  64 00  00 00

// Genesis ID
20 <genesis_32_bytes>

// Extract our input balance
c1                              // OP_ACTIVEBYTECODE (our script)
23 08                           // offset=35, length=8
7f                              // OP_SPLIT
7c 75                           // OP_SWAP OP_DROP
81                              // OP_BIN2NUM (parse balance)

// Sum all output balances with our genesis
00                              // sum = 0
00                              // counter = 0
c4                              // OP_TXOUTPUTCOUNT

c9                              // OP_BEGINLOOP
  // Get output script
  76                            // OP_DUP (dup counter)
  c8                            // OP_OUTPUTBYTECODE
  
  // Extract genesis
  00 20 7f 75                   // Split at 32, drop rest
  
  // Compare with our genesis
  20 <genesis_32_bytes>
  87                            // OP_EQUAL
  
  // If match, add balance
  63                            // OP_IF
    76                          // OP_DUP (dup counter)
    c8                          // OP_OUTPUTBYTECODE
    23 08 7f 7c 75              // Extract balance bytes
    81                          // OP_BIN2NUM
    93                          // OP_ADD (add to sum)
  68                            // OP_ENDIF
  
  51 93                         // OP_1 OP_ADD (counter++)
ca                              // OP_ENDLOOP

75                              // OP_DROP (drop counter)

// Compare: input_balance == output_sum
87 69                           // OP_EQUAL OP_VERIFY

// Signature
76 a9 14 <pkh> 88 ac
```

### Example 2: Access Control List

```
// Verify recipient is in whitelist (max 20 addresses)
f0 e8 03 00 00  14 00  0a 00  64 00  01  14 00  00 00

// Get recipient PKH from output 0
00 c8                           // OP_OUTPUTBYTECODE (output 0)
45 14 7f 7c 75                  // Extract PKH at offset 69

// Whitelist (20 PKHs)
// Loop through whitelist
00                              // found = false
14                              // 20 iterations

c9                              // OP_BEGINLOOP
  // Compare with next whitelist entry
  76                            // OP_DUP (dup recipient PKH)
  <whitelisted_pkh_20_bytes>
  87                            // OP_EQUAL
  
  // If found, set flag
  63                            // OP_IF
    75 51                       // OP_DROP OP_TRUE (found!)
    cb                          // OP_BREAK
  68                            // OP_ENDIF
ca                              // OP_ENDLOOP

75                              // OP_DROP (drop PKH)

// Verify found
69                              // OP_VERIFY

// Signature
76 a9 14 <pkh> 88 ac
```

### Example 3: Programmable Vesting Schedule

```
// Release tokens based on locktime schedule
f0 e8 03 00 00  0c 00  05 00  64 00  01  00 00  00 00

// Vesting schedule: 
// Month 1: 10% (locktime < 1234567890)
// Month 2: 20% (locktime < 1234567890 + 30*24*3600)
// ...
// Month 12: 100%

c5                              // OP_TXLOCKTIME

// Determine vesting percentage based on locktime
<timestamp_month_1>
a1                              // OP_LESSTHANOREQUAL
63                              // OP_IF
  0a                            // 10% → push 10
67                              // OP_ELSE
  <timestamp_month_2>
  a1
  63
    14                          // 20% → push 20
  67
    // ... more months ...
    64                          // 100% → push 100
  68
68

// Stack: vesting_percentage

// Calculate allowed amount: total * percentage / 100
c6                              // OP_UTXOVALUE (our balance)
94                              // OP_MUL
64 96                           // 100 OP_DIV

// Verify output amount <= allowed
00 c7                           // OP_OUTPUTVALUE (output 0)
a2 69                           // OP_LESSTHANOREQUAL OP_VERIFY

// Signature
76 a9 14 <pkh> 88 ac
```

### Example 4: State Machine

```
// Implement state machine with memory
f0 e8 03 00 00  32 00  0a 00  c8 00  02  14 00  00 00

// Load current state from memory
"state" d6                      // OP_MLOAD (load state)

// State machine logic
76                              // OP_DUP
00 87                           // OP_EQUAL (state == 0?)
63                              // OP_IF
  // State 0 → State 1
  75 01                         // Drop, push 1
  "state" 7c d5                 // OP_MSTORE
67                              // OP_ELSE
  01 87                         // state == 1?
  63
    // State 1 → State 2
    75 02
    "state" 7c d5
  67
    // State 2 → State 0 (cycle)
    75 00
    "state" 7c d5
  68
68

// Verify state transition is valid
"state" d6                      // Load new state
02 a2 69                        // Must be <= 2

// Signature
76 a9 14 <pkh> 88 ac
```

---

## 🔐 Part 10: Security Considerations

### 10.1 DoS Prevention

```cpp
// Multiple layers of protection:

1. **Global maximums** - Can't declare bounds > GLOBAL_MAX
2. **Static analysis** - Rejects provably invalid scripts
3. **Miner policy** - Can reject expensive scripts
4. **Fee market** - Expensive scripts pay higher fees
5. **Timeout fallback** - Emergency stop after 10 seconds
```

### 10.2 Consensus Safety

```cpp
// Ensure deterministic execution

// ❌ BANNED - Non-deterministic opcodes
OP_RANDOM              // Would break consensus
OP_TIMESTAMP           // Current time varies
OP_BLOCKHEIGHT_DIRECT  // Height varies

// ✅ ALLOWED - Deterministic only
OP_TXLOCKTIME          // From transaction
OP_CHECKLOCKTIMEVERIFY // Validated against block
OP_UTXOVALUE           // From UTXO
```

### 10.3 Stack Overflow Protection

```cpp
// In EvalScript main loop:
if (stack.size() + altstack.size() > scriptBounds.max_stack) {
    return set_error(serror, ScriptError::STACK_LIMIT_EXCEEDED);
}

// Also enforce global maximum
if (stack.size() + altstack.size() > MAX_STACK_SIZE) {
    return set_error(serror, ScriptError::STACK_SIZE);
}
```

### 10.4 Infinite Loop Prevention

```cpp
// Guaranteed termination through:

1. **Max iterations declared** - Loop can't exceed max
2. **Op count limit** - Total ops enforced
3. **Jump limit** - Can't jump forever
4. **Recursion depth** - Stack depth limited

// Every construct has an upper bound!
```

---

## 🚀 Part 11: Activation & Deployment

### 11.1 Consensus Parameters

**File**: `src/chainparams.cpp`

```cpp
// In CMainParams constructor:
consensus.nTuringScriptActivationHeight = 1250000;  // Set appropriately

// Or BIP9-style activation:
consensus.vDeployments[Consensus::DEPLOYMENT_TURING_SCRIPT].bit = 6;
consensus.vDeployments[Consensus::DEPLOYMENT_TURING_SCRIPT].nStartTime = 1735689600; // Jan 1, 2025
consensus.vDeployments[Consensus::DEPLOYMENT_TURING_SCRIPT].nTimeout = 1767225600;   // Jan 1, 2026
consensus.vDeployments[Consensus::DEPLOYMENT_TURING_SCRIPT].min_activation_height = 1250000;
```

### 11.2 Activation Logic

**File**: `src/consensus/activation.h`

```cpp
enum DeploymentPos {
    // ... existing deployments ...
    DEPLOYMENT_TURING_SCRIPT,
    MAX_VERSION_BITS_DEPLOYMENTS
};

bool IsTuringScriptEnabled(const Consensus::Params &params, int nHeight);
```

**File**: `src/consensus/activation.cpp`

```cpp
bool IsTuringScriptEnabled(const Consensus::Params &params, int nHeight) {
    return nHeight >= params.nTuringScriptActivationHeight;
}
```

### 11.3 Validation Check

**File**: `src/script/interpreter.cpp`

```cpp
// At start of EvalScript:
if (script.size() > 0 && script[0] == OP_BOUNDS_DECLARE) {
    // This is a Turing-script
    
    // Check if Turing-scripts are activated
    // Note: We need block height - pass via checker or flags
    if (!IsTuringScriptEnabled(flags)) {
        return set_error(serror, ScriptError::TURING_SCRIPT_NOT_ACTIVATED);
    }
    
    // Proceed with Turing-script execution
}
```

---

## 📋 Part 12: Complete File Checklist

### New Files to Create

```
src/script/validation.h          - Static analysis interface
src/script/validation.cpp        - Static analysis implementation
src/test/script_turing_tests.cpp - Unit tests
test/functional/feature_turing_script.py - Integration tests
```

### Files to Modify

```
src/script/script.h              - Add 15+ new opcodes
src/script/script.cpp            - Add opcode names
src/script/interpreter.h         - Add ScriptBounds, metrics
src/script/interpreter.cpp       - Implement all opcodes
src/script/script_error.h        - Add new error types
src/script/script_error.cpp      - Add error strings
src/consensus/activation.h       - Add Turing activation flag
src/consensus/activation.cpp     - Implement activation check
src/chainparams.cpp              - Set activation height/params
src/validation.cpp               - Add bounds validation
src/policy/policy.cpp            - Add script cost checking
src/CMakeLists.txt               - Add new source files
```

---

## 🎯 Part 13: Implementation Order

### Phase 1: Core Infrastructure (Week 1)

1. Add `ScriptBounds` struct
2. Implement `ExtractScriptBounds()`
3. Add `ScriptExecutionMetrics` tracking
4. Update `EvalScript()` to track metrics
5. Add global maximums and validation

**Deliverable**: Scripts can declare bounds, metrics tracked

### Phase 2: Loops (Week 1-2)

1. Add `OP_BEGINLOOP`, `OP_ENDLOOP`, `OP_BREAK`, `OP_CONTINUE`, `OP_LOOPINDEX`
2. Implement loop stack
3. Add loop validation
4. Write unit tests

**Deliverable**: Loops work, limited to declared bounds

### Phase 3: Jumps & Labels (Week 2)

1. Add `OP_LABEL`, `OP_JUMP`, `OP_JUMPI`, `OP_JUMPTO`
2. Implement label map building
3. Implement jump validation
4. Write unit tests

**Deliverable**: Jumps work, labels validated

### Phase 4: Functions (Week 2-3)

1. Add `OP_CALL`, `OP_CALLTO`, `OP_RETURN`, `OP_CALLDEPTH`
2. Implement call stack
3. Add recursion depth tracking
4. Write unit tests

**Deliverable**: Function calls work with recursion limits

### Phase 5: Memory (Week 3)

1. Add `OP_MSTORE`, `OP_MLOAD`, `OP_MSIZE`, `OP_MCLEAR`
2. Implement memory map
3. Add memory limit enforcement
4. Write unit tests

**Deliverable**: Scripts have scratch memory

### Phase 6: Static Analysis (Week 3-4)

1. Implement `AnalyzeScript()`
2. Build control flow graph
3. Validate jumps, labels
4. Detect potential issues
5. Write comprehensive tests

**Deliverable**: Scripts pre-validated before execution

### Phase 7: Integration & Testing (Week 4)

1. Integration tests
2. Fuzzing tests
3. Performance benchmarks
4. Security audit
5. Documentation

**Deliverable**: Production-ready implementation

### Phase 8: Deployment (Week 5-6)

1. Testnet deployment
2. Community testing
3. Miner coordination
4. Mainnet activation

**Deliverable**: Live on mainnet

---

## 💎 Part 14: Real-World Use Cases

### Use Case 1: Decentralized Exchange

```
// Order book matching on-chain
- Loop through outputs to find matching orders
- Verify price and quantity
- Execute atomic swap
- All validated by script
```

### Use Case 2: DAO Voting

```
// Count votes from multiple inputs
- Loop through inputs
- Sum voting power
- Verify threshold reached
- Execute action if passed
```

### Use Case 3: NFT Series Validation

```
// Ensure NFT is from valid series
- Check genesis matches collection
- Verify sequence number in range
- Validate metadata hash
- Enforce royalty payment
```

### Use Case 4: Gaming State Updates

```
// Update game state deterministically
- Load previous state from memory
- Apply move/action
- Validate rules
- Store new state
- All on-chain!
```

### Use Case 5: Programmable Vaults

```
// Time-locked release with conditions
- Check locktime vs schedule
- Calculate vested amount
- Verify withdrawal within limit
- Multiple beneficiaries supported
```

---

## 📊 Part 15: Comparison with Other Systems

### vs Ethereum

| Feature | Ethereum | Lotus Turing-Script |
|---------|----------|---------------------|
| **Loops** | ✅ Unbounded (gas) | ✅ Bounded (no gas) |
| **Jumps** | ✅ Yes (gas) | ✅ Yes (declared) |
| **Functions** | ✅ Yes (gas) | ✅ Yes (declared) |
| **Memory** | ✅ Yes (gas) | ✅ Yes (bounded) |
| **Gas fees** | ❌ Required | ✅ Not needed |
| **Halting** | ✅ Guaranteed (gas) | ✅ Guaranteed (bounds) |
| **SPV** | ❌ No | ✅ Yes |
| **UTXO model** | ❌ No | ✅ Yes |

### vs Bitcoin Script

| Feature | Bitcoin | Lotus Turing-Script |
|---------|---------|---------------------|
| **Loops** | ❌ No | ✅ Yes |
| **Introspection** | ❌ No | ✅ Yes |
| **Turing-complete** | ❌ No | ✅ Bounded-equivalent |
| **OP_CAT** | ❌ Disabled | ✅ Enabled |

### vs Bitcoin Cash

| Feature | BCH | Lotus Turing-Script |
|---------|-----|---------------------|
| **OP_CAT** | ✅ Yes | ✅ Yes |
| **Loops** | ❌ No | ✅ Yes |
| **Introspection** | ⚠️ Limited | ✅ Full (9 opcodes) |
| **Covenants** | ⚠️ Proposed | ✅ Implemented |

---

## 🎊 Summary

### What This Implementation Gives You

**Turing-Equivalent Script Execution** with:

✅ **Loops** - Bounded iteration (up to 10,000 iterations)  
✅ **Jumps** - Labeled targets, conditional/unconditional  
✅ **Functions** - Subroutines with recursion (up to 100 levels)  
✅ **Memory** - Scratch space during execution (1,000 slots)  
✅ **Introspection** - Full transaction inspection  
✅ **OP_CAT** - Byte manipulation  
✅ **No gas fees** - Cost known from bounds  
✅ **Deterministic** - Same input = same output  
✅ **Safe** - Guaranteed termination  
✅ **Powerful** - Can express complex logic  

### What You CAN'T Do (Halting Problem)

❌ Truly infinite loops  
❌ Unbounded recursion  
❌ Unknown execution time  
❌ Solve halting problem itself  

**But**: For **all practical purposes**, this is Turing-complete!

### Estimated Implementation Time

- **Minimum viable** (loops only): 1 week
- **Full system** (all opcodes): 4-6 weeks
- **Production ready** (testing, docs): 8-10 weeks

### Lines of Code

- New code: ~2,000 lines
- Modified code: ~500 lines
- Tests: ~1,000 lines
- **Total**: ~3,500 lines

---

## 🚀 Ready to Implement?

This guide provides **complete specifications** for implementing bounded Turing-equivalent script execution in Lotus.

**Start with loops** (OP_BEGINLOOP, OP_ENDLOOP) as they provide the most value with least complexity.

**The node is already 80% there** - you have OP_CAT and introspection opcodes. Adding loops makes it practically Turing-complete!

---

**This would make Lotus the most powerful Bitcoin Script implementation ever created!** 🎉🚀

