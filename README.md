# Fork-Simulator
Simulating what happens when a process forks in C... not a physical fork 

Implimented two fork strategies: one that creates a new page table and copies all page tables, and another that creates a new page table and uses a copy-on-write optimization. Using the Sv39 multi-level page table design.
