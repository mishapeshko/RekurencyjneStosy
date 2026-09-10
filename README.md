# Recursive Stacks Library (C)

A library implementing recursive stacks (stacks that can contain integers or other stacks). Built with the help of Bacon's algorithm.

Key Features
1) Cycle Detection & Garbage Collection: Implements Bacon's Trial Deletion algorithm to accurately identify and clean up isolated cyclic references, preventing memory leaks.
2) Strong Exception Safety: The library guarantees no memory leaks and maintains state integrity even if underlying memory allocations (`malloc`) fail.
3) Custom Memory Testing: Tested thoroughly using `Valgrind` and GCC linker wrappers (`-Wl,--wrap=malloc`) to simulate and survive memory allocation failures.
