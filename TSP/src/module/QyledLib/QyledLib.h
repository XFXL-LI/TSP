/****************************************Copyright (c)**************************************************
**                               Guangzhou QingYuan Infomation Technology Co.,LTD.
**
**                                 http://www.gzqyinfo.cn
**
**--------------File Info-------------------------------------------------------------------------------
** @file File name:					  Qyled.h
** @Last modified Date:				2014/6/3 9:33:23
** @version Last Version:			V1.0
** @Descriptions:		          Apis for QYLED LED-Control-Board
** @Constructor:
**
**
**
**
**------------------------------------------------------------------------------------------------------
** @author Created by:			gdpiao
** @Created date:		        2014/6/3 9:33:58
** @Version:				        V1.0
** @Descriptions:		        Orignal Version
**
**------------------------------------------------------------------------------------------------------
** @Modified by:
** @Modified date:
** @Version:
** @Descriptions:
**
**------------------------------------------------------------------------------------------------------
** @Modified by:
** @Modified date:
** @Version:
** @Descriptions:
**
********************************************************************************************************/
#ifndef __QYLEDLIB_H__
#define __QYLEDLIB_H__

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef IN_QYLEDLIB_H
#define QYLEDLIB_EXT extern
#else
#define QYLEDLIB_EXT
#endif

#include <stdint.h>
/***********************************************
**    define  marcos 宏定义
************************************************/
//*****************************************************************************
//
// Helper Macros for Ethernet Processing
//
//*****************************************************************************
//
// htonl/ntohl - big endian/little endian byte swapping macros for
// 32-bit (long) values
//
//*****************************************************************************
#ifndef INT2BYTE
#if LITTL_ENDIAN > 0
#define INT2BYTE(a)               \
    ((((a) >> 24) & 0x000000ff) | \
     (((a) >> 8) & 0x0000ff00) |  \
     (((a) << 8) & 0x00ff0000) |  \
     (((a) << 24) & 0xff000000))
#else
#define INT2BYTE(a) (a)
#endif
#endif
//*****************************************************************************
// htonl/ntohl - big endian/little endian byte swapping macros for
// 32-bit (long) values
//*****************************************************************************
#ifndef htonl

#define htonl(a)                  \
    ((((a) >> 24) & 0x000000ff) | \
     (((a) >> 8) & 0x0000ff00) |  \
     (((a) << 8) & 0x00ff0000) |  \
     (((a) << 24) & 0xff000000))
#endif

#ifndef ntohl
#define ntohl(a) (a)
#endif
//*****************************************************************************
// htons/ntohs - big endian/little endian byte swapping macros for
// 16-bit (short) values
//*****************************************************************************
#ifndef htons

#define htons(a)             \
    ((((a) >> 8) & 0x00ff) | \
     (((a) << 8) & 0xff00))

#endif

#ifndef ntohs
#define ntohs(a) (a)
#endif

/***********************************************
**       COMMAND LIST
************************************************/
//!
#define RS232_DB_PAGES_ID (0x72957992)
#define RS232_UPDATE_DATA (0x65)
#define RS232_DB_PAGES (0x66)
#define RS232_DB_CONTEXT (0x67)
#define RS232_ADJBRIGHT (0x76)

//! 485 COMMAND LIST
#define RS485_DB_CONTEXT (0x35)
#define RS485_DB_PAGES (0x36)
#define RS485_UPDATE_DATA (0x37)
#define RS485_ADJBRIGHT (0x76)

///--------Protocol  define
#define QYLED_PROTO_RS232 (0)
#define QYLED_PROTO_RS485 (1)

/***********************************************
**        struct
************************************************/
#pragma pack(1)
    typedef struct _NetHead_T
    {
        uint32_t PreCode; // 前导码--固定0xfe5c4b89 --
        uint32_t Len;     // 命令总长度-----包括前导码和后导码
        uint8_t Command;  // 命令字
        uint32_t ID;      // 消息ID
        uint32_t CommLen; // 该类命令内容长度

    } NetHead_T;
    typedef struct _RS485_Head_t_
    {
        uint8_t PreCode; // 前导码--固定0xfe
        uint8_t DstAddr; // 目标地址
        uint16_t Len;    // 命令总长度-----不包括前导码和校验字
        uint8_t SrcAddr; // 源地址
        uint8_t Command; // 命令字
        uint8_t Rsv1;    // 保留字 填0
        uint8_t Mac[8];  // 485物理地址---一共有三种形式 1：内部码 2：前面7个是0 第8个字节是地址 3：全部0 广播包
    } RS485_Head_t;

    typedef struct _RT_Signal_RS485_Head
    {
        uint8_t PreCode; // 前导码--固定0xfe
        uint8_t DstAddr; // 目标地址
        uint16_t Len;    // 命令总长度-----不包括前导码和校验字
        uint8_t SrcAddr; // 源地址
        uint8_t Command; // 命令字
        uint8_t Rsv1;    // 保留字 填0
        uint8_t Mac[8];  // 485物理地址---一共有三种形式 1：内部码 2：前面7个是0 第8个字节是地址 3：全部0 广播包

        // uint32 ID;            //消息ID
        // uint32 CommLen;        //该类命令内容长度
    } RT_Signal_RS485_Head;
    typedef struct _DB_Context_HEAD
    {
        uint8_t Index;       // 点播数据项
        uint8_t IndexBak;    // 点播数据项补码
        uint8_t RefreshMode; // 显示模式 0--立即更新 1-稍后更新
        uint8_t Rsv1;        // 保留2个字节
        uint8_t Rsv2;
    } DB_Context_HEAD;

    typedef struct DB_Context_
    {
        uint8_t AreaNumber;  // 区域号码
        uint16_t PicStart;   // 图片开始地址 0开始
        uint16_t PicTotle;   // 显示屏数量
        uint8_t DisplayMode; // 保留3字节 走字方式 移动速度 停留时间
        uint8_t Speed;
        uint8_t PauseTime;
    } DB_Context;

    typedef struct REAL_DATA_
    {

        uint8_t Index; // 采集编号
        uint8_t Rsv;
        uint8_t Color;
        uint8_t Font;
        uint8_t Len;
        // uint8_t LenofPlayload;
    } REAL_DATA;

    typedef struct _Produce_t_
    {
        uint8_t Rsv0;        // 保留0
        uint8_t LineNum;     // 生产线编号
        uint8_t ClassNum;    // 生产组编号
        uint8_t ProduceNum;  // 生产单号-工单
        uint8_t Type;        // 数据类型
        uint8_t Index;       // 种类编号--对应显示的内容
        uint8_t IsTimes;     // 是否跟随节拍变化
        uint8_t IsSend2Disp; // 是否送LED显示屏
        uint8_t DispMode1;   // 显示方式1
        uint8_t DispMode2;   // 显示方式2
        uint8_t DispMode3;   // 显示方式3
        uint8_t Fraction;    // 计算值小数位
        uint8_t Rsv1[4];     // 保留1
        uint8_t DataLen;     // 数据区域长度--跟着的是初始化数据区
    } Produce_t;
#pragma pack()

    /************************************************
    **    private val 私有变量定义
    ************************************************/

    /************************************************
    **    public val 公共变量定义
    ************************************************/

    /************************************************
    **    private fuctions 私有函数
    ************************************************/

    /************************************************
    **    public fuctions 公共函数
    ************************************************/
    QYLEDLIB_EXT uint16_t packQYLEDTextByRS232(uint8_t *dst, uint16_t lenofdst, uint8_t index, uint8_t *src, uint8_t lenofsrc);
    QYLEDLIB_EXT void TestLed(void);

    QYLEDLIB_EXT uint8_t setRealData(uint8_t *dst, uint8_t Index, uint8_t Color, uint8_t Font, uint8_t Len);
    QYLEDLIB_EXT uint16_t packNetBuffer(uint8_t *dst, uint8_t cmd, uint32_t id, uint8_t *src_con, uint16_t conlen);
    QYLEDLIB_EXT uint16_t packQYLEDText2Server(uint8_t *dst, uint16_t lenofdst, uint8_t index, uint8_t *src, uint8_t lenofsrc);
    QYLEDLIB_EXT uint16_t packQYLEDContext2Server(uint8_t *dst, uint8_t cmd, uint32_t id, uint8_t subcmd, uint8_t res, uint8_t *context, uint16_t lenofcontext);
    QYLEDLIB_EXT uint16_t pack485Buffer(uint8_t *dst, uint8_t cmd, uint8_t *mac, uint8_t *src_con, uint16_t conlen);

    QYLEDLIB_EXT uint8_t packSelectDisplayPage(uint8_t *src, uint8_t no);
    QYLEDLIB_EXT uint8_t fillLevel(uint8_t protoc, uint8_t prio, uint8_t level, uint8_t *dst);
    QYLEDLIB_EXT uint8_t pack_db_Context(uint8_t *dst, uint8_t area_no, uint16_t index_start, uint16_t numbers);
    QYLEDLIB_EXT uint8_t modify_db_Context_PalyMode(uint8_t *dst, uint8_t dispmode, uint8_t speed, uint8_t ptime);
    QYLEDLIB_EXT uint8_t pack_adj_brightness(uint8_t *ptr, uint8_t prio, uint8_t level);

    // QYLEDLIB_EXT   uint16_t pack_tts_db(uint8_t *dst,uint8_t index,uint8_t addr);
    QYLEDLIB_EXT uint16_t pack_tts_db(uint32_t tts_base, uint16_t tts_max, uint8_t *dst, uint8_t index, uint8_t addr);
    QYLEDLIB_EXT uint16_t pack_ttsbuffer_by_index(uint32_t tts_base, uint16_t tts_max, uint8_t *dst, uint16_t maxlen, uint8_t index);
    // QYLEDLIB_EXT   uint16_t pack_ttsbuffer_by_index(uint8_t *dst,uint16_t maxlen,uint8_t index);

    QYLEDLIB_EXT uint8_t check_sum(uint8_t *s_addr, uint16_t len);
// QYLEDLIB_EXT   uint8_t format_string(uint8_t *dst,uint8_t *src,uint8_t mode);
#ifdef __cplusplus
}
#endif
#endif
/*******************************************************************************************************************
**
**          End of file
**
*******************************************************************************************************************/
