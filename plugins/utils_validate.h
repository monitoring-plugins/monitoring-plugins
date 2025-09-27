#ifndef NP_UTILS_VALIDATE_H
#define NP_UTILS_VALIDATE_H
/* Header file for Monitoring Plugins utils_validate.c */

/* This file should be included in all plugins where user
input needs to be validated */

/* The purpose of this package is to provide reusable logic
for the purpose of validating user input. */

void validate_file_exists(char *path);
#endif /* NP_UTILS_VALIDATE_H */
