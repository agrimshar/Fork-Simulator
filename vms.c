#include "vms.h"

#include "mmu.h"
#include "pages.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

int page_reference_count[MAX_PAGES] = {0};

/* A debugging helper that will print information about the pointed to PTE
   entry. */
static void print_pte_entry(uint64_t* entry);

void page_fault_handler(void* virtual_address, int level, void* page_table) {
    // Get the page table entry
    uint64_t* pte = vms_page_table_pte_entry(page_table, virtual_address, level);

    print_pte_entry(pte);

    // If the page is not valid, exit
    if (!vms_pte_valid(pte))
    {
        exit(EFAULT);
    }

    // If no custom bit is set, exit
    if (!vms_pte_custom(pte))
    {
        exit(EFAULT);
    }

    // If no read bit is set, exit
    if (!vms_pte_read(pte))
    {
        exit(EFAULT);
    }

    // If the custom bit is set
    if (vms_pte_custom(pte))
    {
        // Get the PPN of the faulting page
        uint64_t ppn = vms_pte_get_ppn(pte);
        void* old_page = vms_ppn_to_page(ppn);
        int page_index = vms_get_page_index(old_page);

        // If multiple pages are sharing the same page, copy-on-write
        if (page_reference_count[page_index] > 1)
        {
            // Make a new page for the faulting page
            void* new_page = vms_new_page();

            // Point the PTE to the new page
            vms_pte_set_ppn(pte, vms_page_to_ppn(new_page));

            // Make the new page writeable
            vms_pte_write_set(pte);

            // Copy the data from the old page to the new page
            memcpy(new_page, old_page, PAGE_SIZE);

            // Decrement the reference count of the old page
            page_reference_count[page_index]--;

            // Increment the reference count of the new page
            page_reference_count[vms_get_page_index(new_page)] = 1;

            // Only clear the custom bit if the reference count is 1
            if (page_reference_count[page_index] == 1)
            {
                vms_pte_custom_clear(pte);
            }
        }
        else 
        {
            // If the reference count is 1, just set the page to be readable, writeable, and valid
            vms_pte_write_set(pte);

            // Clear the custom bit
            vms_pte_custom_clear(pte);
        }
    }
}

void* vms_fork_copy(void) {
    // Create a new l2 page table
    void* parent_l2 = vms_get_root_page_table();

    // This is the new root page table for the child process
    void* child_l2 = vms_new_page();

    // Iterate over every entry in the parent l2 page table
    for (int i = 0; i < NUM_PTE_ENTRIES; ++i)
    {
        // Get the PTE pointing to the L1 PT from l2
        uint64_t* parent_l2_entry = vms_page_table_pte_entry_from_index(parent_l2, i);

        // If the PTE is valid, we need to copy the L1 PT
        if (vms_pte_valid(parent_l2_entry))
        {
            // Create a new child L1 PT
            void* child_l1 = vms_new_page();

            // Set the PPN of the child L1 PT in the child L2 PT
            uint64_t* child_l2_entry = vms_page_table_pte_entry_from_index(child_l2, i);

            // Copy the PPN from the parent L2 PT to the child L2 PT
            vms_pte_set_ppn(child_l2_entry, vms_page_to_ppn(child_l1));

            // Copy the data from the parent L1 PT to the child L1 PT
            memcpy(child_l1, vms_ppn_to_page(vms_pte_get_ppn(parent_l2_entry)), PAGE_SIZE);

            // Set the PTE flags in the child L2 PT
            vms_pte_valid_set(child_l2_entry);

            // Iterate over every entry in the child L1 PT
            for (int j = 0; j < NUM_PTE_ENTRIES; ++j)
            {
                // Get the PTE pointing to the L0 PT from the L1 PT
                uint64_t* parent_l1_entry = vms_page_table_pte_entry_from_index(vms_ppn_to_page(vms_pte_get_ppn(parent_l2_entry)), j);

                // If the PTE is valid, we need to copy the L0 PT
                if (vms_pte_valid(parent_l1_entry))
                {
                    // Create a new child L0 PT
                    void* child_l0 = vms_new_page();

                    // Set the PPN of the child L0 PT in the child L1 PT
                    uint64_t* child_l1_entry = vms_page_table_pte_entry_from_index(child_l1, j);

                    // Copy the PPN from the parent L1 PT to the child L1 PT
                    vms_pte_set_ppn(child_l1_entry, vms_page_to_ppn(child_l0));

                    // Copy the data from the parent L0 PT to the child L0 PT
                    memcpy(child_l0, vms_ppn_to_page(vms_pte_get_ppn(parent_l1_entry)), PAGE_SIZE);

                    // Set the PTE flags in the child L1 PT
                    vms_pte_valid_set(child_l1_entry);

                    // Iterate over every entry in the child L0 PT
                    for (int k = 0; k < NUM_PTE_ENTRIES; ++k)
                    {
                        // Get the PTE pointing to the page from the L0 PT
                        uint64_t* parent_l0_entry = vms_page_table_pte_entry_from_index(vms_ppn_to_page(vms_pte_get_ppn(parent_l1_entry)), k);

                        // If the PTE is valid, we need to copy the page
                        if (vms_pte_valid(parent_l0_entry))
                        {
                            // Create a new child page
                            void* child_page = vms_new_page();

                            // Set the PPN of the child page in the child L0 PT
                            uint64_t* child_l0_entry = vms_page_table_pte_entry_from_index(child_l0, k);

                            // Copy the PPN from the parent L0 PT to the child L0 PT
                            vms_pte_set_ppn(child_l0_entry, vms_page_to_ppn(child_page));

                            // Copy the data from the parent page to the child page
                            memcpy(child_page, vms_ppn_to_page(vms_pte_get_ppn(parent_l0_entry)), PAGE_SIZE);

                            // Set the PTE flags in the child L0 PT
                            vms_pte_valid_set(child_l0_entry);
                            if (vms_pte_read(parent_l0_entry))
                            {
                                vms_pte_read_set(child_l0_entry);
                            }
                            if (vms_pte_write(parent_l0_entry))
                            {
                                vms_pte_write_set(child_l0_entry);
                            }
                            if (vms_pte_custom(parent_l0_entry))
                            {
                                vms_pte_custom_set(child_l0_entry);
                            }
                        }
                    }
                }
            }

        }
    }

    return child_l2;
}

void* vms_fork_copy_on_write(void) {
    void* parent_l2 = vms_get_root_page_table();

    // This is the new root page table for the child process
    void* child_l2 = vms_new_page();

    // Iterate over every entry in the parent l2 page table
    for (int i = 0; i < NUM_PTE_ENTRIES; ++i)
    {
        // Get the parent L2 PTE
        uint64_t* parent_l2_entry = vms_page_table_pte_entry_from_index(parent_l2, i);

        // If the PTE is valid, ew need to make the page read only for both parent and child
        if (vms_pte_valid(parent_l2_entry))
        {
            // Create a new child l1 page table
            void* child_l1 = vms_new_page();

            // Get the child L2 PTE
            uint64_t* child_l2_entry = vms_page_table_pte_entry_from_index(child_l2, i);

            // Copy the PPN from the parent L2 PT to the child L2 PT
            vms_pte_set_ppn(child_l2_entry, vms_page_to_ppn(child_l1));

            // Set the PTE flags in the child L2 PT
            vms_pte_valid_set(child_l2_entry);

            // Iterate over every entry in the parent L1 PT
            for (int j = 0; j < NUM_PTE_ENTRIES; ++j)
            {
                // Get the parent L1 PTE
                uint64_t* parent_l1_entry = vms_page_table_pte_entry_from_index(vms_ppn_to_page(vms_pte_get_ppn(parent_l2_entry)), j);

                // If the PTE is valid, we need to make the page read only for both parent and child
                if (vms_pte_valid(parent_l1_entry))
                {
                    // Create a new child L0 PT
                    void* child_l0 = vms_new_page();

                    // Get the child L1 PTE
                    uint64_t* child_l1_entry = vms_page_table_pte_entry_from_index(child_l1, j);

                    // Copy the PPN from the parent L1 PT to the child L1 PT
                    vms_pte_set_ppn(child_l1_entry, vms_page_to_ppn(child_l0));

                    // Set the PTE flags in the child L1 PT
                    vms_pte_valid_set(child_l1_entry);

                    // Irate over every entry in the parent L0 PT
                    for (int k = 0; k < NUM_PTE_ENTRIES; ++k)
                    {
                        // Get the parent L0 PTE
                        uint64_t* parent_l0_entry = vms_page_table_pte_entry_from_index(vms_ppn_to_page(vms_pte_get_ppn(parent_l1_entry)), k);

                        // If the PTE is valid, we need to make the page read only for both parent and child
                        if (vms_pte_valid(parent_l0_entry))
                        {
                            // Get the PPN of the parent page
                            uint64_t ppn = vms_pte_get_ppn(parent_l0_entry);

                            // Get the child L0 PTE
                            uint64_t* child_l0_entry = vms_page_table_pte_entry_from_index(child_l0, k);

                            // Copy the PPN from the parent L0 PT to the child L0 PT
                            vms_pte_set_ppn(child_l0_entry, ppn);

                            // Set the PTE flags in the child L0 PT
                            vms_pte_valid_set(child_l0_entry);

                            if (vms_pte_write(parent_l0_entry))
                            {
                                // Set the custom bit in the parent and child L0 PT
                                vms_pte_custom_set(parent_l0_entry);
                                vms_pte_custom_set(child_l0_entry);

                                // Clear the write bit in the parent and child L0 PT
                                vms_pte_write_clear(parent_l0_entry);
                                vms_pte_write_clear(child_l0_entry);

                                // Set the read bit in the parent and child L0 PT
                                if (vms_pte_read(parent_l0_entry))
                                {
                                    vms_pte_read_set(child_l0_entry);
                                }
                            } 
                            else
                            {
                                // Set all the child bits same as the parent bits
                                if (vms_pte_read(parent_l0_entry))
                                {
                                    vms_pte_read_set(child_l0_entry);
                                }
                                if (vms_pte_write(parent_l0_entry))
                                {
                                    vms_pte_write_set(child_l0_entry);
                                }
                                if (vms_pte_custom(parent_l0_entry))
                                {
                                    vms_pte_custom_set(child_l0_entry);
                                }
                            }

                            // Increment the reference count of the parent and child page
                            ++page_reference_count[vms_get_page_index(vms_ppn_to_page(ppn))];
                            ++page_reference_count[vms_get_page_index(vms_ppn_to_page(vms_pte_get_ppn(child_l0_entry)))];
                        }
                    }
                }
            }
        }
    }

    return child_l2;
}

static void print_pte_entry(uint64_t* entry) {
    const char* dash = "-";
    const char* custom = dash;
    const char* write = dash;
    const char* read = dash;
    const char* valid = dash;
    if (vms_pte_custom(entry)) {
        custom = "C";
    }
    if (vms_pte_write(entry)) {
        write = "W";
    }
    if (vms_pte_read(entry)) {
        read = "R";
    }
    if (vms_pte_valid(entry)) {
        valid = "V";
    }

    printf("PPN: 0x%lX Flags: %s%s%s%s\n",
           vms_pte_get_ppn(entry),
           custom, write, read, valid);
}
