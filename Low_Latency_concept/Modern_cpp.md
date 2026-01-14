# C++ Core Guidelines - Complete Reference

A comprehensive guide based on the C++ Core Guidelines by Bjarne Stroustrup. This document provides essential rules and best practices for writing modern, safe, and efficient C++ code.

## Table of Contents

- [1. The Basics](#1-the-basics)
- [2. User-Defined Types](#2-user-defined-types)
- [3. Modularity](#3-modularity)
- [4. Error Handling](#4-error-handling)
- [5. Classes](#5-classes)
- [6. Essential Operations](#6-essential-operations)
- [7. Templates](#7-templates)
- [8. Concepts and Generic Programming](#8-concepts-and-generic-programming)
- [9. Library Overview](#9-library-overview)
- [10. Strings and Regular Expressions](#10-strings-and-regular-expressions)
- [11. Input and Output](#11-input-and-output)
- [12. Containers](#12-containers)
- [13. Algorithms](#13-algorithms)
- [14. Ranges](#14-ranges)
- [15. Pointers and Containers](#15-pointers-and-containers)
- [16. Utilities](#16-utilities)
- [17. Numbers](#17-numbers)
- [18. Concurrency](#18-concurrency)
- [19. History and Compatibility](#19-history-and-compatibility)
- [Appendix A. Modules](#appendix-a-modules)

---

## 1. The Basics

<details>
<summary><strong>Core Programming Principles</strong></summary>

1. All will become clear in time.
2. **Don't use the built-in features exclusively.** Many fundamental (built-in) features are usually best used indirectly through libraries, such as the ISO C++ standard library.
3. **`#include` or (preferably) `import`** the libraries needed to simplify programming.
4. **You don't have to know every detail** of C++ to write good programs.
5. **Focus on programming techniques,** not on language features.
6. **The ISO C++ standard** is the final word on language definition issues.

</details>

### Functions
7. **"Package" meaningful operations** as carefully named functions.
8. **A function should perform a single logical operation.**
9. **Keep functions short.**
10. **Use overloading** when functions perform conceptually the same task on different types.
11. **If a function may have to be evaluated at compile time,** declare it `constexpr`.
12. **If a function must be evaluated at compile time,** declare it `consteval`.
13. **If a function may not have side effects,** declare it `constexpr` or `consteval`.
14. **Understand how language primitives map to hardware.**

---

## 2. User-Defined Types

1. **Prefer well-defined user-defined types** over built-in types when the built-in types are too low-level.
2. **Organize related data** into structures (`struct`s or `class`es).
3. **Represent the distinction** between an interface and an implementation using a `class`.
4. **A `struct` is simply a `class`** with its members public by default.
5. **Define constructors** to guarantee and simplify initialization of classes.
6. **Use enumerations** to represent sets of named constants.
7. **Prefer class enums** over "plain" enums to minimize surprises.
8. **Define operations on enumerations** for safe and simple use.
9. **Avoid "naked" unions;** wrap them in a class together with a type field.
10. **Prefer `std::variant`** to "naked unions."

---

## 3. Modularity

<details>
<summary><strong>Interface and Implementation Design</strong></summary>

1. **Distinguish between declarations** (used as interfaces) and definitions (used as implementations).
2. **Prefer modules over headers** (where modules are supported).
3. **Use header files** to represent interfaces and to emphasize logical structure.
4. **`#include` a header** in the source file that implements its functions.
5. **Avoid non-inline function definitions** in headers.
6. **Use namespaces** to express logical structure.
7. **Use using-directives** for transition, for foundational libraries (such as `std`), or within a local scope.
8. **Don't put a using-directive** in a header file.

</details>

### Parameter Passing
9. **Pass "small" values by value** and "large" values by reference.
10. **Prefer pass-by-const-reference** over plain pass-by-reference.
11. **Return values as function-return values** (rather than by out-parameters).
12. **Don't overuse return-type deduction.**
13. **Don't overuse structured binding;** a named return type often gives more readable code.

---

## 4. Error Handling

### Exception Strategy
1. **Throw an exception** to indicate that you cannot perform an assigned task.
2. **Use exceptions for error handling only.**
3. **Failing to open a file** or to reach the end of an iteration are expected events and not exceptional.
4. **Use error codes** when an immediate caller is expected to handle the error.
5. **Throw an exception** for errors expected to percolate up through many function calls.
6. **If in doubt** whether to use an exception or an error code, prefer exceptions.
7. **Develop an error-handling strategy** early in a design.
8. **Use purpose-designed user-defined types** as exceptions (not built-in types).

### Exception Handling
9. **Don't try to catch every exception** in every function.
10. **You don't have to use** the standard-library exception class hierarchy.
11. **Prefer RAII** to explicit try-blocks.
12. **Let a constructor establish an invariant,** and throw if it cannot.
13. **Design your error-handling strategy** around invariants.

### Compile-time Checking
14. **What can be checked at compile time** is usually best checked at compile time.
15. **Use an assertion mechanism** to provide a single point of control of the meaning of failure.
16. **Concepts are compile-time predicates** and therefore often useful in assertions.
17. **If your function may not throw,** declare it `noexcept`.
18. **Don't apply `noexcept` thoughtlessly.**

---

## 5. Classes

### Design Principles
1. **Express ideas directly in code.**
2. **A concrete type is the simplest kind of class.** Where applicable, prefer a concrete type over more complicated classes and over plain data structures.
3. **Use concrete classes** to represent simple concepts.
4. **Prefer concrete classes** over class hierarchies for performance-critical components.

### Construction and Members
5. **Define constructors** to handle initialization of objects.
6. **Make a function a member only** if it needs direct access to the representation of a class.
7. **Define operators** primarily to mimic conventional usage.
8. **Use nonmember functions** for symmetric operators.
9. **Declare a member function** that does not modify the state of its object `const`.

### Resource Management
10. **If a constructor acquires a resource,** its class needs a destructor to release the resource.
11. **Avoid "naked" `new` and `delete` operations.**
12. **Use resource handles and RAII** to manage resources.
13. **If a class is a container,** give it an initializer-list constructor.

<details>
<summary><strong>Inheritance and Polymorphism</strong></summary>

14. **Use abstract classes as interfaces** when complete separation of interface and implementation is needed.
15. **Access polymorphic objects** through pointers and references.
16. **An abstract class** typically doesn't need a constructor.
17. **Use class hierarchies** to represent concepts with inherent hierarchical structure.
18. **A class with a virtual function** should have a virtual destructor.
19. **Use `override`** to make overriding explicit in large class hierarchies.
20. **When designing a class hierarchy,** distinguish between implementation inheritance and interface inheritance.
21. **Use `dynamic_cast`** where class hierarchy navigation is unavoidable.
22. **Use `dynamic_cast` to a reference type** when failure to find the required class is considered a failure.
23. **Use `dynamic_cast` to a pointer type** when failure to find the required class is considered a valid alternative.
24. **Use `unique_ptr` or `shared_ptr`** to avoid forgetting to delete objects created using `new`.

</details>

---

## 6. Essential Operations

### Rule of Five/Zero
1. **Control construction, copy, move, and destruction** of objects.
2. **Design constructors, assignments, and the destructor** as a matched set of operations.
3. **Define all essential operations or none.**
4. **If a default constructor, assignment, or destructor is appropriate,** let the compiler generate it.
5. **If a class has a pointer member,** consider if it needs a user-defined or deleted destructor, copy and move.
6. **If a class has a user-defined destructor,** it probably needs user-defined or deleted copy and move.

### Construction Best Practices
7. **By default, declare single-argument constructors `explicit`.**
8. **If a class member has a reasonable default value,** provide it as a data member initializer.
9. **Redefine or prohibit copying** if the default is not appropriate for a type.
10. **Return containers by value** (relying on copy elision and move for efficiency).

<details>
<summary><strong>Advanced Operations</strong></summary>

11. **Avoid explicit use of `std::copy`.**
12. **For large operands,** use const reference argument types.
13. **Provide strong resource safety;** that is, never leak anything that you think of as a resource.
14. **If a class is a resource handle,** it needs a user-defined constructor, a destructor, and non-default copy operations.
15. **Manage all resources** – memory and non-memory – resources using RAII.
16. **Overload operations** to mimic conventional usage.
17. **If you overload an operator,** define all operations that conventionally work together.
18. **If you define `<=>` for a type as non-default,** also define `==`.
19. **Follow the standard-library container design.**

</details>

---

## 7. Templates

### Template Usage
1. **Use templates** to express algorithms that apply to many argument types.
2. **Use templates** to express containers.
3. **Use templates** to raise the level of abstraction of code.
4. **Templates are type safe,** but for unconstrained templates checking happens too late.
5. **Let constructors or function templates** deduce class template argument types.

### Function Objects and Lambdas
6. **Use function objects** as arguments to algorithms.
7. **Use a lambda** if you need a simple function object in one place only.
8. **A virtual function member** cannot be a template member function.
9. **Use `finally`** to provide RAII for types without destructors that require "cleanup operations".

### Advanced Templates
10. **Use template aliases** to simplify notation and hide implementation details.
11. **Use `if constexpr`** to provide alternative implementations without run-time overhead.

---

## 8. Concepts and Generic Programming

### Design Philosophy
1. **Templates provide a general mechanism** for compile-time programming.
2. **When designing a template,** carefully consider the concepts (requirements) assumed for its template arguments.
3. **When designing a template,** use a concrete version for initial implementation, debugging, and measurement.
4. **Use concepts as a design tool.**
5. **Specify concepts** for all template arguments.

### Concept Usage
6. **Whenever possible use named concepts** (e.g., standard-library concepts).
7. **Use a lambda** if you need a simple function object in one place only.
8. **Use templates** to express containers and ranges.
9. **Avoid "concepts"** without meaningful semantics.
10. **Require a complete set of operations** for a concept.
11. **Use named concepts.**
12. **Avoid `requires requires`.**
13. **`auto` is the least constrained concept.**

<details>
<summary><strong>Advanced Generic Programming</strong></summary>

14. **Use variadic templates** when you need a function that takes a variable number of arguments of a variety of types.
15. **Templates offer compile-time "duck typing".**
16. **When using header files,** `#include` template definitions (not just declarations) in every translation unit that uses them.
17. **To use a template,** make sure its definition (not just its declaration) is in scope.
18. **Unconstrained templates** offer compile-time "duck typing".

</details>

---

## 9. Library Overview

1. **Don't reinvent the wheel;** use libraries.
2. **When you have a choice,** prefer the standard library over other libraries.
3. **Do not think** that the standard library is ideal for everything.
4. **If you don't use modules,** remember to `#include` the appropriate headers.
5. **Remember** that standard-library facilities are defined in namespace `std`.
6. **When using ranges,** remember to explicitly qualify algorithm names.
7. **Prefer importing modules** over `#include`ing header files.

---

## 10. Strings and Regular Expressions

### String Usage
1. **Use `std::string`** to own character sequences.
2. **Prefer string operations** to C-style string functions.
3. **Use `string`** to declare variables and members rather than as a base class.
4. **Return strings by value** (rely on move semantics and copy elision).
5. **Directly or indirectly,** use `substr` to read substrings and `replace` to write substrings.
6. **A string can grow and shrink,** as needed.

### String Operations
7. **Use `at`** rather than iterators or `[]` when you want range checking.
8. **Use iterators and `[]`** rather than `at` when you want to optimize speed.
9. **Use a range-for** to safely minimize range checking.
10. **`string` input doesn't overflow.**
11. **Use `c_str` or `data`** to produce a C-style string representation of a string (only) when you have to.
12. **Use a `stringstream`** or a generic value extraction function (such as `to<X>`) for numeric conversion of strings.

<details>
<summary><strong>String Types and Views</strong></summary>

13. **A `basic_string` can be used** to make strings of characters on any type.
14. **Use the `s` suffix** for string literals meant to be standard-library strings.
15. **Use `string_view`** as an argument of functions that needs to read character sequences stored in various ways.
16. **Use `string_span<char>`** as an argument of functions that needs to write character sequences stored in various ways.
17. **Think of a `string_view`** as a kind of pointer with a size attached; it does not own its characters.
18. **Use the `sv` suffix** for string literals meant to be standard-library `string_view`s.

</details>

### Regular Expressions
19. **Use `regex`** for most conventional uses of regular expressions.
20. **Prefer raw string literals** for expressing all but the simplest patterns.
21. **Use `regex_match`** to match a complete input.
22. **Use `regex_search`** to search for a pattern in an input stream.
23. **The regular expression notation** can be adjusted to match various standards.
24. **The default regular expression notation** is that of ECMAScript.
25. **Be restrained;** regular expressions can easily become a write-only language.
26. **Note that `\i` for a digit `i`** allows you to express a subpattern in terms of a previous subpattern.
27. **Use `?`** to make patterns "lazy".
28. **Use `regex_iterator`s** for iterating over a stream looking for a pattern.

---

## 11. Input and Output

### Basic I/O Principles
1. **iostreams are type-safe,** type-sensitive, and extensible.
2. **Use character-level input** only when you have to.
3. **When reading,** always consider ill-formed input.
4. **Avoid `endl`** (if you don't know what `endl` is, you haven't missed anything).
5. **Define `<<` and `>>`** for user-defined types with values that have meaningful textual representations.
6. **Use `cout` for normal output** and `cerr` for errors.

### Stream Types and Operations
7. **There are iostreams** for ordinary characters and wide characters, and you can define an iostream for any kind of character.
8. **Binary I/O is supported.**
9. **There are standard iostreams** for standard I/O streams, files, and strings.
10. **Chain `<<` operations** for a terser notation.
11. **Chain `>>` operations** for a terser notation.
12. **Input into strings does not overflow.**
13. **By default `>>` skips initial whitespace.**

<details>
<summary><strong>Stream State and Error Handling</strong></summary>

14. **Use the stream state `fail`** to handle potentially recoverable I/O errors.
15. **We can define `<<` and `>>` operators** for our own types.
16. **We don't need to modify `istream` or `ostream`** to add new `<<` and `>>` operators.
17. **Use manipulators or `format`** to control formatting.
18. **precision specifications apply** to all following floating-point output operations.
19. **Floating-point format specifications** (e.g., scientific) apply to all following floating-point output operations.
20. **`#include <ios>` or `<iostream>`** when using standard manipulators.
21. **Stream formatting manipulators are "sticky"** for use for many values in a stream.
22. **`#include <iomanip>`** when using standard manipulators taking arguments.

</details>

### Advanced I/O
23. **We can output time, dates, etc.** in standard formats.
24. **Don't try to copy a stream:** streams are move only.
25. **Remember to check** that a file stream is attached to a file before using it.
26. **Use stringstreams or memory streams** for in-memory formatting.
27. **We can define conversions** between any two types that both have string representation.
28. **C-style I/O is not type-safe.**
29. **Unless you use printf-family functions** call `ios_base::sync_with_stdio(false)`.
30. **Prefer `<filesystem>`** to direct use of platform-specific interfaces.

---

## 12. Containers

### Container Basics
1. **An STL container defines a sequence.**
2. **STL containers are resource handles.**
3. **Use `vector` as your default container.**
4. **For simple traversals of a container,** use a range-for loop or a begin/end pair of iterators.
5. **Use `reserve`** to avoid invalidating pointers and iterators to elements.
6. **Don't assume performance benefits** from `reserve` without measurement.

### Vector Operations
7. **Use `push_back` or `resize`** on a container rather than `realloc` on an array.
8. **Don't use iterators** into a resized vector.
9. **Do not assume that `[]` range checks.**
10. **Use `at`** when you need guaranteed range checks.
11. **Use range-for and standard-library algorithms** for cost-free avoidance of range errors.
12. **Elements are copied into a container.**

<details>
<summary><strong>Container Selection and Performance</strong></summary>

13. **To preserve polymorphic behavior of elements,** store pointers (built-in or user-defined).
14. **Insertion operations,** such as `insert` and `push_back`, are often surprisingly efficient on a `vector`.
15. **Use `forward_list`** for sequences that are usually empty.
16. **When it comes to performance,** don't trust your intuition: measure.
17. **A `map` is usually implemented** as a red-black tree.
18. **An `unordered_map` is a hash table.**
19. **Pass a container by reference** and return a container by value.
20. **For a container,** use the `()`-initializer syntax for sizes and the `{}`-initializer syntax for sequences of elements.

</details>

### Container Choice Guidelines
21. **Prefer compact and contiguous data structures.**
22. **A `list` is relatively expensive to traverse.**
23. **Use unordered containers** if you need fast lookup for large amounts of data.
24. **Use ordered containers** (e.g., `map` and `set`) if you need to iterate over their elements in order.
25. **Use unordered containers** (e.g., `unordered_map`) for element types with no natural order (i.e., no reasonable `<`).
26. **Use associative containers** (e.g., `map` and `list`) when you need pointers to elements to be stable as the size of the container changes.

### Hash and Memory
27. **Experiment to check** that you have an acceptable hash function.
28. **A hash function obtained by combining** standard hash functions for elements using the exclusive-or operator (`^`) is often good.
29. **Know your standard-library containers** and prefer them to handcrafted data structures.
30. **If your application is suffering performance problems** related to memory, minimize free store use and/or consider using a specialized allocator.

---

## 13. Algorithms

### Algorithm Basics
1. **An STL algorithm operates** on one or more sequences.
2. **An input sequence is half-open** and defined by a pair of iterators.
3. **You can define your own iterators** to serve special needs.
4. **Many algorithms can be applied** to I/O streams.
5. **When searching,** an algorithm usually returns the end of the input sequence to indicate "not found".
6. **Algorithms do not directly add or subtract elements** from their argument sequences.

### Algorithm Usage
7. **When writing a loop,** consider whether it could be expressed as a general algorithm.
8. **Use using-type-aliases** to clean up messy notation.
9. **Use predicates and other function objects** to give standard algorithms a wider range of meanings.
10. **A predicate must not modify its argument.**
11. **Know your standard-library algorithms** and prefer them to hand-crafted loops.

---

## 14. Ranges

### Range Algorithms
1. **When the pair-of-iterators style becomes tedious,** use a range algorithm.
2. **When using a range algorithm,** remember to explicitly introduce its name.
3. **Pipelines of operations on a range** can be expressed using views, generators, and filters.
4. **To end a range with a predicate,** you need to define a sentinel.
5. **Using `static_assert`,** we can check that a specific type meets the requirements of a concept.
6. **If you want a range algorithm** and there isn't one in the standard, just write your own.

### Range Concepts
7. **The ideal for types is regular.**
8. **Prefer standard-library concepts** where they apply.
9. **When requesting parallel execution,** be sure to avoid data races and deadlock.

---

## 15. Pointers and Containers

### Smart Pointers
1. **A library doesn't have to be large or complicated** to be useful.
2. **A resource is anything** that has to be acquired and (explicitly or implicitly) released.
3. **Use resource handles** to manage resources (RAII).
4. **The problem with a `T*`** is that it can be used to represent anything, so we cannot easily determine a "raw" pointer's purpose.
5. **Use `unique_ptr`** to refer to objects of polymorphic type.
6. **Use `shared_ptr`** to refer to shared objects (only).
7. **Prefer resource handles with specific semantics** to smart pointers.
8. **Don't use a smart pointer** where a local variable will do.

### Smart Pointer Best Practices
9. **Prefer `unique_ptr` to `shared_ptr`.**
10. **Use `unique_ptr` or `shared_ptr`** as arguments or return values only to transfer ownership responsibilities.
11. **Use `make_unique`** to construct `unique_ptr`s.
12. **Use `make_shared`** to construct `shared_ptr`s.
13. **Prefer smart pointers** to garbage collection.

<details>
<summary><strong>Arrays and Utility Types</strong></summary>

14. **Prefer spans** to pointer-plus-count interfaces.
15. **`span` supports range-for.**
16. **Use `array`** where you need a sequence with a `constexpr` size.
17. **Prefer `array`** over built-in arrays.
18. **Use `bitset`** if you need N bits and N is not necessarily the number of bits in a built-in integer type.
19. **Don't overuse `pair` and `tuple`;** named structs often lead to more readable code.
20. **When using `pair`,** use template argument deduction or `make_pair` to avoid redundant type specification.
21. **When using `tuple`,** use template argument deduction or `make_tuple` to avoid redundant type specification.
22. **Prefer `variant`** to explicit use of unions.
23. **When selecting among a set of alternatives** using a `variant`, consider using `visit` and `overloaded`.
24. **If more than one alternative is possible** for a `variant`, `optional`, or `any`, check the tag before access.

</details>

---

## 16. Utilities

### Time and Measurement
1. **A library doesn't have to be large or complicated** to be useful.
2. **Time your programs** before making claims about efficiency.
3. **Use `duration_cast`** to report time measurements with proper units.
4. **To represent a date directly in source code,** use symbolic notation (e.g., `November/28/2021`).
5. **If a date is a result of a computation,** check for validity using `ok`.
6. **When dealing with time in different locations,** use `zoned_time`.

### Function Objects and Type Traits
7. **Use a lambda** to express minor changes in calling conventions.
8. **Use `mem_fn` or a lambda** to create function objects that can invoke a member function when called using the traditional function call notation.
9. **Use `function`** when you need to store something that can be called.
10. **Prefer concepts** to explicit use of type predicates.
11. **You can write code** to explicitly depend on properties of types.
12. **Prefer concepts over traits** and `enable_if` whenever you can.

<details>
<summary><strong>Advanced Utilities</strong></summary>

13. **Use `source_location`** to embed source code locations in debug and logging messages.
14. **Avoid explicit use of `std::move`.**
15. **Use `std::forward`** exclusively for forwarding.
16. **Never read from an object** after `std::move`ing or `std::forward`ing it.
17. **Use `std::byte`** to represent data that doesn't (yet) have a meaningful type.
18. **Use unsigned integers or bitsets** for bit manipulation.
19. **Return an error-code from a function** if the immediate caller can be expected to handle the problem.
20. **Throw an exception from a function** if the immediate caller cannot be expected to handle the problem.
21. **Call `exit`, `quick_exit`, or `terminate`** to exit a program if an attempt to recover from a problem is not reasonable.
22. **No general-purpose library** should unconditionally terminate.

</details>

---

## 17. Numbers

### Numerical Computing
1. **Numerical problems are often subtle.** If you are not 100% certain about the mathematical aspects of a numerical problem, either take expert advice, experiment, or do both.
2. **Don't try to do serious numeric computation** using only the bare language; use libraries.
3. **Consider `accumulate`, `inner_product`, `partial_sum`, and `adjacent_difference`** before you write a loop to compute a value from a sequence.
4. **For larger amounts of data,** try the parallel and vectorized algorithms.
5. **Use `std::complex`** for complex arithmetic.

### Random Numbers
6. **Bind an engine to a distribution** to get a random number generator.
7. **Be careful** that your random numbers are sufficiently random for your intended use.
8. **Don't use the C standard-library `rand`;** it isn't insufficiently random for real uses.

### Numeric Types
9. **Use `valarray`** for numeric computation when run-time efficiency is more important than flexibility with respect to operations and element types.
10. **Properties of numeric types** are accessible through `numeric_limits`.
11. **Use `numeric_limits`** to check that the numeric types are adequate for their use.
12. **Use aliases for integer types** if you want to be specific about their sizes.

---

## 18. Concurrency

### Concurrency Strategy
1. **Use concurrency** to improve responsiveness or to improve throughput.
2. **Work at the highest level of abstraction** that you can afford.
3. **Consider processes** as an alternative to threads.
4. **The standard-library concurrency facilities** are type safe.
5. **The memory model exists** to save most programmers from having to think about the machine architecture level of computers.
6. **The memory model makes memory appear** roughly as naively expected.

### Lock-free Programming
7. **Atomics allow for lock-free programming.**
8. **Leave lock-free programming to experts.**
9. **Sometimes, a sequential solution** is simpler and faster than a concurrent solution.
10. **Avoid data races.**
11. **Prefer parallel algorithms** to direct use of concurrency.

### Threads and Synchronization
12. **A thread is a type-safe interface** to a system thread.
13. **Use `join`** to wait for a thread to complete.
14. **Prefer `jthread` over `thread`.**
15. **Avoid explicitly shared data** whenever you can.
16. **Prefer RAII** to explicit lock/unlock.
17. **Use `scoped_lock`** to manage mutexes.
18. **Use `scoped_lock`** to acquire multiple locks.
19. **Use `shared_lock`** to implement reader-write locks.
20. **Define a mutex together** with the data it protects.

<details>
<summary><strong>Advanced Concurrency</strong></summary>

21. **Use atomics** for very simple sharing.
22. **Use `condition_variable`s** to manage communication among threads.
23. **Use `unique_lock`** (rather than `scoped_lock`) when you need to copy a lock or need lower-level manipulation of synchronization.
24. **Use `unique_lock`** (rather than `scoped_lock`) with `condition_variable`s.
25. **Don't wait without a condition.**
26. **Minimize time spent** in a critical section.
27. **Think in terms of concurrent tasks,** rather than directly in terms of threads.
28. **Value simplicity.**
29. **Prefer `packaged_task`s and futures** over direct use of threads and mutexes.
30. **Return a result using a `promise`** and get a result from a `future`.
31. **Use `packaged_task`s** to handle exceptions thrown by tasks.
32. **Use a `packaged_task` and a `future`** to express a request to an external service and wait for its response.
33. **Use `async`** to launch simple tasks.
34. **Use `stop_token`** to implement cooperative termination.
35. **A coroutine can be very much smaller** than a thread.
36. **Prefer coroutine support libraries** to hand-crafted code.

</details>

---

## 19. History and Compatibility

### Standards and Guidelines
1. **The ISO C++ standard defines C++.**
2. **When choosing a style** for a new project or when modernizing a code base, rely on the C++ Core Guidelines.
3. **When learning C++,** don't focus on language features in isolation.
4. **Don't get stuck** with decades-old language-feature sets and design techniques.
5. **Before using a new feature in production code,** try it out by writing small programs to test the standards conformance and performance of the implementations you plan to use.
6. **For learning C++,** use the most up-to-date and complete implementation of Standard C++ that you can get access to.

### C Compatibility
7. **The common subset of C and C++** is not the best initial subset of C++ to learn.
8. **Avoid casts.**
9. **Prefer named casts,** such as `static_cast` over C-style casts.
10. **When converting a C program to C++,** rename variables that are C++ keywords.
11. **For portability and type safety,** if you must use C, write in the common subset of C and C++.

<details>
<summary><strong>C to C++ Migration</strong></summary>

12. **When converting a C program to C++,** cast the result of `malloc` to the proper type or change all uses of `malloc` to uses of `new`.
13. **When converting from `malloc` and `free`** to `new` and `delete`, consider using `vector`, `push_back`, and `reserve` instead of `realloc`.
14. **In C++, there are no implicit conversions** from `int`s to enumerations; use explicit type conversion where necessary.
15. **For each standard C header `<X.h>`** that places names in the global namespace, the header `<cX>` places the names in namespace `std`.
16. **Use `extern "C"`** when declaring C functions.
17. **Prefer `string`** over C-style strings (direct manipulation of zero-terminated arrays of `char`).
18. **Prefer iostreams** over stdio.
19. **Prefer containers** (e.g., `vector`) over built-in arrays.

</details>

---

## Appendix A. Modules

1. **Prefer modules** provided by an implementation.
2. **Use modules.**
3. **Prefer named modules** over header units.
4. **To use the macros and global names** from the C standard, `import std.compat`.
5. **Avoid macros.**

---