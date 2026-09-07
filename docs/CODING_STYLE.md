# C++ coding and documentation standard

This project uses a Beningo-inspired coding style adapted to modern C++23, Qt, and
the existing public QML interface. The goal is consistent, reviewable code rather
than a claim of formal MISRA or AUTOSAR compliance.

## Formatting

- Use UTF-8 source files, LF line endings, four spaces, and no tabs.
- Limit lines to 100 characters where practical.
- Put every opening and closing brace on its own line (Allman style).
- Always use braces around `if`, `else`, `for`, `while`, and `do` bodies.
- Attach pointer and reference declarators to the type (`QObject* parent`,
  `const QString& value`) as shown in `BeningoStyleTemplate.hpp`.
- Keep one statement per line and use whitespace to separate logical blocks.
- Group includes as project, Qt, and standard-library headers. Keep each group sorted.
- Run `cmake --build <build-dir> --target format` before review. CI or reviewers can
  use the non-mutating `format-check` target.

The repository `.clang-format` file is the executable definition of these rules.
Qt Creator can use it through **Preferences > C++ > Code Style > ClangFormat**.
New modules should start from `docs/templates/BeningoStyleTemplate.hpp` and
`docs/templates/BeningoStyleTemplate.cpp`.

## Naming

- Classes, structs, aliases, and enums use `UpperCamelCase`.
- Functions, methods, signals, slots, and local variables use `lowerCamelCase`.
- Private data members use the `m_` prefix.
- Compile-time constants use `UpperCamelCase`.
- Boolean names state a condition (`isReady`, `m_running`, `success`).
- Existing Qt/QML property and invokable names are API and must remain stable.

## C++ and Qt rules

- Prefer RAII and value semantics. Use smart pointers for owning heap objects.
- Raw pointers are non-owning unless explicitly documented otherwise.
- Use `nullptr`, scoped lifetimes, `const`, `noexcept`, and `[[nodiscard]]` where
  their contracts apply.
- Do not block the Qt event loop with network, process, or long-running work.
- Do not log tokens, passwords, authorization codes, or other credentials.
- Validate data at subsystem boundaries and return actionable errors.

## Doxygen comments

- Every header uses a unique include guard and starts with the project file banner:
  `@file`, `@brief`, `@author`, `@date`, `@version`, copyright, and target toolchain.
- Document public classes, structs, functions, methods, signals, and slots.
- Begin each declaration with an explicit one-sentence `@brief` ending in a period.
- Use `@param[in]`, `@param[out]`, or `@param[in,out]` for every parameter and
  `@return` for meaningful return values.
- State ownership, thread-affinity, persistence, and security constraints when they
  are part of the contract.
- Implementation comments explain *why* a decision exists, not what the syntax says.
- Keep documentation beside the declaration so refactoring cannot separate it from
  the API.

Generate the HTML reference with:

```powershell
cmake -S . -B build/docs -DBUILD_DOCUMENTATION=ON
cmake --build build/docs --target docs
```

The entry page is `build/docs/documentation/html/index.html`.

## Review checklist

1. Formatting and `format-check` pass.
2. New public API has complete Doxygen contracts.
3. Doxygen completes without warnings.
4. Compiler warnings and automated tests pass.
5. Secrets and user data are absent from diagnostics and documentation.
