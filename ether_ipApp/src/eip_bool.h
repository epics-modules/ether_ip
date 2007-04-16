#ifndef EIP_BOOL_H_
#define EIP_BOOL_H_

/* Andrew Johnson (ANL) found an issue with bool/true/false
 * and a PPC604-with-Altivec compiler, so we use eip_bool
 * and are careful not to redefine 'true'.
 */
typedef int eip_bool;
#ifndef __cplusplus
  #ifndef true
    #define true  1
    #define false 0
  #endif
#endif

#endif /*EIP_BOOL_H_*/
