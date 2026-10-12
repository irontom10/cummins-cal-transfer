#ifndef DA00_SENDKEY_H
#define DA00_SENDKEY_H

/* ISO C89 offline UDS SecurityAccess cryptogram builder.
   Pure calculation; does not communicate with any ECU. */
int da00_sendkey(const unsigned char seed[32],
                 const unsigned char aes_key[16],
                 unsigned int tool_version,
                 unsigned long tool_instance,
                 const unsigned char tool_family[6],
                 int include_optional,
                 unsigned char optional_byte,
                 unsigned char out[32]);

#endif
