#pragma once

enum page_fault_error_code {
  PF_ERR_PRESENT = 1 << 0,  // protection violation, not a non-present page
  PF_ERR_WRITE = 1 << 1,    // Write access, not a read access
  PF_ERR_USER = 1 << 2,     // User-mode access, not a supervisor-mode access
  PF_ERR_RSVD = 1 << 3,  // Reserved bit violation, not a reserved bit violation
  PF_ERR_IFETCH = 1 << 4  // Instruction fetch, not a data access
};
