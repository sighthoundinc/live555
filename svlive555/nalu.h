#ifndef __NALU_H
#define __NALU_H

#include <stdint.h>

namespace sio {

namespace live555 {


void        svlive555_parse_sps               ( unsigned char * pStart,
                                                  unsigned short _nLen,
                                                  int* w, int* h, int* profile,
                                                  int* level );



}; // namespace live555

}; // namespace sio

#endif // __NALU_H

