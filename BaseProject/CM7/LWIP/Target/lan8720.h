#ifndef LAN8720_H
#define LAN8720_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Board strap: PHYAD=1 (confirmed by MDIO scan) */
#define LAN8720_PHY_ADDRESS        1U

#define LAN8720_BCR                  0x00U
#define LAN8720_BSR                  0x01U
#define LAN8720_PHYSCSR              0x1FU

#define LAN8720_BCR_SOFT_RESET       0x8000U
#define LAN8720_BCR_AUTONEGO_EN      0x1000U
#define LAN8720_BCR_POWER_DOWN       0x0800U
#define LAN8720_BCR_RESTART_AUTONEGO 0x0200U

#define LAN8720_BSR_LINK_STATUS      0x0004U
#define LAN8720_BSR_AUTONEGO_CPLT    0x0020U

#define LAN8720_PHYSCSR_AUTONEGO_DONE 0x1000U
#define LAN8720_PHYSCSR_HCDSPEEDMASK  0x001CU
#define LAN8720_PHYSCSR_10BT_HD       0x0004U
#define LAN8720_PHYSCSR_10BT_FD       0x0014U
#define LAN8720_PHYSCSR_100BTX_HD     0x0008U
#define LAN8720_PHYSCSR_100BTX_FD     0x0018U

#define LAN8720_STATUS_READ_ERROR           ((int32_t)-5)
#define LAN8720_STATUS_WRITE_ERROR          ((int32_t)-4)
#define LAN8720_STATUS_RESET_TIMEOUT        ((int32_t)-2)
#define LAN8720_STATUS_ERROR                ((int32_t)-1)
#define LAN8720_STATUS_OK                   ((int32_t) 0)
#define LAN8720_STATUS_LINK_DOWN            ((int32_t) 1)
#define LAN8720_STATUS_100MBITS_FULLDUPLEX  ((int32_t) 2)
#define LAN8720_STATUS_100MBITS_HALFDUPLEX  ((int32_t) 3)
#define LAN8720_STATUS_10MBITS_FULLDUPLEX   ((int32_t) 4)
#define LAN8720_STATUS_10MBITS_HALFDUPLEX    ((int32_t) 5)
#define LAN8720_STATUS_AUTONEGO_NOTDONE     ((int32_t) 6)

typedef int32_t (*lan8720_Init_Func)(void);
typedef int32_t (*lan8720_DeInit_Func)(void);
typedef int32_t (*lan8720_ReadReg_Func)(uint32_t, uint32_t, uint32_t *);
typedef int32_t (*lan8720_WriteReg_Func)(uint32_t, uint32_t, uint32_t);
typedef int32_t (*lan8720_GetTick_Func)(void);

typedef struct
{
  lan8720_Init_Func Init;
  lan8720_DeInit_Func DeInit;
  lan8720_WriteReg_Func WriteReg;
  lan8720_ReadReg_Func ReadReg;
  lan8720_GetTick_Func GetTick;
} lan8720_IOCtx_t;

typedef struct
{
  uint32_t DevAddr;
  uint32_t Is_Initialized;
  lan8720_IOCtx_t IO;
  void *pData;
} lan8720_Object_t;

int32_t LAN8720_RegisterBusIO(lan8720_Object_t *pObj, lan8720_IOCtx_t *ioctx);
int32_t LAN8720_Init(lan8720_Object_t *pObj);
int32_t LAN8720_DeInit(lan8720_Object_t *pObj);
int32_t LAN8720_GetLinkState(lan8720_Object_t *pObj);
void LAN8720_HardwareReset(void);

#ifdef __cplusplus
}
#endif

#endif /* LAN8720_H */
