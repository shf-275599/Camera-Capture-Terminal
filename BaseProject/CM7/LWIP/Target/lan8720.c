#include "lan8720.h"
#include "main.h"

#define LAN8720_RESET_TIMEOUT_MS  500U

void LAN8720_HardwareReset(void)
{
#ifdef LAN8720_RST_GPIO_Port
  HAL_GPIO_WritePin(LAN8720_RST_GPIO_Port, LAN8720_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(50);
  HAL_GPIO_WritePin(LAN8720_RST_GPIO_Port, LAN8720_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10);
#else
  /* PHY nRST not wired to MCU; rely on module pull-up and soft reset */
  HAL_Delay(100);
#endif
}

int32_t LAN8720_RegisterBusIO(lan8720_Object_t *pObj, lan8720_IOCtx_t *ioctx)
{
  if ((pObj == NULL) || (ioctx == NULL) || (ioctx->ReadReg == NULL) ||
      (ioctx->WriteReg == NULL) || (ioctx->GetTick == NULL))
  {
    return LAN8720_STATUS_ERROR;
  }

  pObj->IO.Init = ioctx->Init;
  pObj->IO.DeInit = ioctx->DeInit;
  pObj->IO.ReadReg = ioctx->ReadReg;
  pObj->IO.WriteReg = ioctx->WriteReg;
  pObj->IO.GetTick = ioctx->GetTick;

  return LAN8720_STATUS_OK;
}

int32_t LAN8720_Init(lan8720_Object_t *pObj)
{
  uint32_t regvalue = 0U;
  uint32_t tickstart = 0U;
  int32_t status = LAN8720_STATUS_OK;

  if (pObj->Is_Initialized != 0U)
  {
    return LAN8720_STATUS_OK;
  }

  LAN8720_HardwareReset();

  if (pObj->IO.Init != NULL)
  {
    pObj->IO.Init();
  }

  if (pObj->DevAddr > 31U)
  {
    pObj->DevAddr = LAN8720_PHY_ADDRESS;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &regvalue) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  if ((regvalue & LAN8720_BCR_SOFT_RESET) == 0U)
  {
    regvalue |= LAN8720_BCR_AUTONEGO_EN;
    regvalue &= (uint32_t)~LAN8720_BCR_POWER_DOWN;

    if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, regvalue) < 0)
    {
      return LAN8720_STATUS_WRITE_ERROR;
    }

    pObj->Is_Initialized = 1U;
    return status;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, LAN8720_BCR_SOFT_RESET) < 0)
  {
    return LAN8720_STATUS_WRITE_ERROR;
  }

  tickstart = pObj->IO.GetTick();
  do
  {
    if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &regvalue) < 0)
    {
      return LAN8720_STATUS_READ_ERROR;
    }

    if ((pObj->IO.GetTick() - tickstart) > LAN8720_RESET_TIMEOUT_MS)
    {
      return LAN8720_STATUS_RESET_TIMEOUT;
    }
  } while ((regvalue & LAN8720_BCR_SOFT_RESET) != 0U);

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &regvalue) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  regvalue |= LAN8720_BCR_AUTONEGO_EN;
  regvalue &= (uint32_t)~LAN8720_BCR_POWER_DOWN;

  if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, regvalue) < 0)
  {
    return LAN8720_STATUS_WRITE_ERROR;
  }

  pObj->Is_Initialized = 1U;
  return status;
}

int32_t LAN8720_DeInit(lan8720_Object_t *pObj)
{
  if (pObj->Is_Initialized != 0U)
  {
    if ((pObj->IO.DeInit != NULL) && (pObj->IO.DeInit() < 0))
    {
      return LAN8720_STATUS_ERROR;
    }

    pObj->Is_Initialized = 0U;
  }

  return LAN8720_STATUS_OK;
}

int32_t LAN8720_GetLinkState(lan8720_Object_t *pObj)
{
  uint32_t readval = 0U;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BSR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BSR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  if ((readval & LAN8720_BSR_LINK_STATUS) == 0U)
  {
    return LAN8720_STATUS_LINK_DOWN;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_PHYSCSR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  if ((readval & LAN8720_PHYSCSR_AUTONEGO_DONE) == 0U)
  {
    return LAN8720_STATUS_AUTONEGO_NOTDONE;
  }

  switch (readval & LAN8720_PHYSCSR_HCDSPEEDMASK)
  {
  case LAN8720_PHYSCSR_100BTX_FD:
    return LAN8720_STATUS_100MBITS_FULLDUPLEX;
  case LAN8720_PHYSCSR_100BTX_HD:
    return LAN8720_STATUS_100MBITS_HALFDUPLEX;
  case LAN8720_PHYSCSR_10BT_FD:
    return LAN8720_STATUS_10MBITS_FULLDUPLEX;
  default:
    return LAN8720_STATUS_10MBITS_HALFDUPLEX;
  }
}
