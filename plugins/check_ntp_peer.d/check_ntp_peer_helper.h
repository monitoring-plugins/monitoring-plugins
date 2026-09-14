/*
 * Extract the value from key/value pairs, or return NULL. The value returned
 * can be free()ed.
 * This function can be used to parse NTP control packet data and performance
 * data strings.
 */
char *check_ntp_peer_extract_value(const char *, const char *, char);

/*
 * Same as np_extract_value with separator suitable for NTP control packet
 * payloads (comma)
 */
char *check_ntp_peer_extract_ntpvar(const char *, const char *n);
