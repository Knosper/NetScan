# Coding Guidelines

These guidelines define the default coding style for this project.  
They are intentionally strict and should be treated as implementation rules, not suggestions.

## 1. Core Principles

- **Single Responsibility**: Each function does exactly one thing.
- **Small Units**: `<= 30` LOC (`<= 40` for algorithmic complexity).
- **Deterministic**: No hidden side-effects.
- **Readability** over cleverness.

## 2. Function Structure

Use linear, guard-clause-first functions wherever possible:

```cpp
ReturnType function_name(Type a, Type b)
{
    if (!valid(a))
        return error;

    auto result = compute(a, b);
    return result;
}
```

### Rules

- Max `3` parameters. Use a `struct` / DTO otherwise.
- Max `2` nesting levels.

## 3. Naming

### C++

| Element  | Style        | Example             |
|----------|--------------|---------------------|
| Function | `snake_case` | `compact_sstables`  |
| Class    | `PascalCase` | `MemTable`          |
| Variable | `snake_case` | `key_size`          |
| Constant | `UPPER_CASE` | `MAX_LEVEL`         |

### JavaScript

| Element  | Style        | Example                    |
|----------|--------------|----------------------------|
| Function | `camelCase`  | `fetchWithTimeout`         |
| Class    | `PascalCase` | `ScanService`              |
| Variable | `camelCase`  | `currentView`              |
| Constant | `UPPER_CASE` | `DEFAULT_FETCH_TIMEOUT_MS` |

## 4. No Redundancy

Avoid duplication in both behavior and representation:

- No duplicated logic across functions or branches.
- No duplicated control flow patterns, for example repeated `if` / `return` blocks.
- No duplicated mapping logic, for example status-to-result mapping in multiple places.
- No duplicated string literals with the same meaning.

### Rules

- Extract shared logic into a single helper.
- Each decision must exist in exactly one place.
- Avoid copy-paste patterns, even if they are short.
- No forward declarations.

