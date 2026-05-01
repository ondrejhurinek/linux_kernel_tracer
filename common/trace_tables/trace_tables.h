/*System call table as important from the
  kernel documentation version 5.15.0*/

#ifndef SYSCALL_NAMES_H
#define SYSCALL_NAMES_H

#define SYSCALL_NO 548

/* arrays used in this module */
const char* syscall_names[SYSCALL_NO];
const char* syscall_abi[SYSCALL_NO];

/* functions to be exported from this module */
const char* get_syscall_name(int i);
const char* get_syscall_abi(int i);

#endif
