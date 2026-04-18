/*
 * Copyright 2023 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "zb_platform.h"
#include "AHI_AES.h"
#include "rnd_pub.h"
#include "AppHardwareApi.h"
#include "aessw_ccm.h"

extern PUBLIC void vECB_Decrypt(uint8* au8Key, uint8* au8InData, uint8* au8OutData);

void zbPlatCryptoInit(void)
{
}

void zbPlatCryptoDeInit(void)
{
}

uint8_t zbPlatCryptoRandomInit(void)
{
#if (defined JENNIC_CHIP_FAMILY_JN516x) || (defined JENNIC_CHIP_FAMILY_JN517x)
    uint32 u32RandomSeed = 0;

    /* Initialise random random number generator with a seed */
    /* store wake interrupt enable register */
#if JENNIC_CHIP_FAMILY == JN516x
    uint32 u32Temp = *((volatile uint32 *)0x0200100C);

    *((volatile uint32 *)0x02001010) |= (1 << 30); /* clear ready flag */
#endif

    vAHI_StartRandomNumberGenerator(TRUE /* single shot */, FALSE /* no ints */);
    while (!bAHI_RndNumPoll());
    u32RandomSeed = ((uint32)u16AHI_ReadRandomNumber()) << 16;
#if JENNIC_CHIP_FAMILY== JN516x
    *((volatile uint32 *)0x02001010) |= (1 << 30); /* clear ready flag */
#endif
    vAHI_StartRandomNumberGenerator(TRUE /* single shot */, FALSE /* no ints */);
    while (!bAHI_RndNumPoll());
    u32RandomSeed |= u16AHI_ReadRandomNumber();
#if JENNIC_CHIP_FAMILY == JN516x
    /* restore wake interrupt enable register */
    *((volatile uint32 *)0x0200100C) = u32Temp;

#endif
    RND_vInit(u32RandomSeed);
#endif
    return 0;
}

uint32_t zbPlatCryptoRandomGet(uint32_t u32Min, uint32_t u32Max)
{
    return RND_u32GetRand(u32Min, u32Max);
}

uint32_t zbPlatCryptoRandom256Get(void)
{
    return RND_u32GetRand256();
}

void zbPlatCryptoAesHmacMmo(uint8_t *pu8Data, int iDataLen, void *key, void *hash)
{
    return AESSW_vHMAC_MMO(pu8Data, iDataLen, (AESSW_Block_u *)key, (AESSW_Block_u *)hash);
}

void zbPlatCryptoAesMmoBlockUpdate(void *hash, void *block)
{
    return AESSW_vMMOBlockUpdate((AESSW_Block_u *)hash, (AESSW_Block_u *)block);
}

void zbPlatCryptoAesMmoFinalUpdate(void *hash, uint8_t *pu8Data, int iDataLen, int iFinalLen)
{
    return AESSW_vMMOFinalUpdate((AESSW_Block_u *)hash, pu8Data, iDataLen, iFinalLen);
}

void zbPlatCryptoAes128EcbEncrypt(const uint8_t* pu8Input, uint32_t u32InputLen,
		const uint8_t* pu8Key, uint8_t* pu8Output)
{
    bACI_ECBencodeStripe((tsReg128 *)pu8Key, TRUE, (tsReg128 *)pu8Input, (tsReg128 *)pu8Output);

    return;
}

void zbPlatCryptoAesDecrypt(const uint8_t* pu8Input, const uint8_t* pu8Key, uint8_t* pu8Output)
{
    return vECB_Decrypt((uint8_t *)pu8Key, (uint8_t *)pu8Input, pu8Output);
}

bool_t zbPlatCryptoAesSetKey(CRYPTO_tsReg128 *psKeyData)
{
#if (defined LITTLE_ENDIAN_PROCESSOR) && (JENNIC_CHIP_FAMILY == JN517x)
    CRYPTO_tsReg128 sKeyRevIn;
    vSwipeEndian(psKeyData,&sKeyRevIn,TRUE);
    bAesReturn = bACI_WriteKey(&sKeyRevIn);
#else
    return bACI_WriteKey((tsReg128 *)psKeyData);
#endif
}

void zbPlatCryptoAesCcmStar(bool_t bEncrypt, uint8_t u8M, uint8_t  u8AuthLen,
		uint8_t u8InputLen, CRYPTO_tsAesBlock *puNonce, uint8_t *pu8AuthData,
		uint8_t *pu8Input, uint8_t *pu8ChecksumData, bool_t *pbChecksumVerify)
{
    return vACI_OptimisedCcmStar(bEncrypt, u8M, u8AuthLen, u8InputLen, (tuAES_Block *)puNonce,
            pu8AuthData, pu8Input, pu8ChecksumData, pbChecksumVerify);
}
