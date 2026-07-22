/****************************************Copyright (c)**************************************************
**                               Guangzhou QingYuan Infomation Technology Co.,LTD.
**
**                                 http://www.gzqyinfo.cn
**
**--------------File Info-------------------------------------------------------------------------------
** @file File name:					  Qyled.c
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

#define IN_QYLEDLIB_H
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "Qyledlib.h"

/*******************************************************************************************************************
@funciton:  【函数名称】	static uint8_t check_sum(uint8_t *s_addr,uint16_t len)
@brief      【函数描述】	XOR异或检验和

@param [in] 【参数描述】    s_addr 入口 len 校验长度
@return     【返回值】      XOR 异或和
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************************************************/
uint8_t check_sum(uint8_t *s_addr, uint16_t len)
{
	uint8_t checksum;
	uint16_t i;
	checksum = 0;
	for (i = 0; i < len; i++)
		checksum = checksum ^ *(s_addr + i);
	return (checksum);
}
/*******************************************************************************************************************
** 函数名称:   uint8_t format_string(uint8_t *dst,uint8_t *src,uint8_t mode)
**
** 功能描述:   格式化输出
**
** 输　  入:   src:  字符串 必须以0结尾
			   dst:  输出缓冲
			   mode: 模式--bit0-3 对齐长度 1-16位
						   bit4:5 对齐方式 00-左对齐 01 右对齐 10 居中 11 保留
						   bit6   前补零显示
**
** 输 　 出:
**
** 返 回 值:
********************************************************************************************************************/
// uint8_t format_string(uint8_t *dst,uint8_t *src,uint8_t mode)
//{
//	uint8_t buff[20];
//	uint8_t pad_f,pad_end;
//	uint8_t len;
//	uint8_t totle_len;   //! 返回长度
//	uint8_t algin;       //! 对齐方式
//	uint8_t i;
//	uint8_t j;
//
//	len = strlen((char *)src);
//	if(len>=16)
//		return 0x0; //不做任何处理---长度过长
//
//	memset(buff,0,sizeof(buff));
//	totle_len = ((mode)&0x0f)+1;  //! range (1-16)
//	algin     = ((mode>>4)&0x03); //! 对齐方式
//	//! 完全拷贝
//	if(len >=totle_len)
//		algin = 0x03;
//	switch(algin)
//	{
//		//! 右对齐
//		case 0x1:
//			pad_f = (totle_len - len);
//			pad_end = 0;
//			break;
////		//! 左对齐
////		case 0x00:
////			pad_f = = 0;
////			pad_end = 0;
////			break;
//		//! 居中对齐
//		case 0x02:
//			pad_f = (totle_len - len)/2;
//			pad_end = totle_len - len - pad_f;
//			break;
//		//! 默认不做处理
//		default:
//			pad_f   = 0;
//			pad_end = 0;
//			break;
//	}
//
//	j=0;
//	//! 前面补充空白
//	for(i=0;i<pad_f;i++)
//	{
//		buff[j++] = ' ';
//	}
//	//! 中间补充数据
//	for(i=0;i<len;i++)
//	{
//		buff[j++] = *src++;
//	}
//	//! 后补充空白
//	for(i=0;i<pad_end;i++)
//	{
//		buff[j++] = ' ';
//	}

//	totle_len = strlen((char *)buff); //总长度
//	for(i=0;i<totle_len;i++)
//		*(dst+i) = buff[i];
//	return totle_len; //成功处理，返回处理后的长度
//}
/*******************************************************************************************************************
@funciton:  【函数名称】	uint16_t packQYLEDTextByRS232(uint8_t *dst,uint16_t lenofdst, uint8_t index,uint8_t *src,uint8_t lenofsrc)
@brief      【函数描述】	把字符串打包成QYLED串口协议格式

@param [in] 【参数描述】 dst 目标字符串 lenofdst 目标字符串总长度  context 要显示的字符串包含index等内容
						 lenofcontext 要显示的字符串长度
@return     【返回值】   uint16_t  长度为打包后的dst有效内容的总长度
@retval     【返回值】
@remarks    【注意事项】  lenofsrc 最大值为16，返回值最大值为300 并且2个数据包之间的时间间隔最少为10ms.
						  推荐使用50ms以确保LED控制卡识别出数据帧
@note       【注意事项】
@see        【其他参考项】
********************************************************************************************************************/
uint16_t packQYLEDContext2Server(uint8_t *dst, uint8_t cmd, uint32_t id, uint8_t subcmd, uint8_t res, uint8_t *context, uint16_t lenofcontext)
{
	uint8_t *ptr;
	uint8_t i;
	uint32_t total_len;
	// 参数检测
	if (dst == NULL) // 参数不符合要求
		return 0x0;

	ptr = dst;
	*ptr++ = 0xfe; // 前导码
	*ptr++ = 0x5c;
	*ptr++ = 0x4b;
	*ptr++ = 0x89;
	*ptr++ = 0x00; // totle len frame len
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x00; // len  4Bytes 低位在前高位在后

	*ptr++ = cmd;	   // CMD--实时采集命令---
	*ptr++ = id;	   // ID0
	*ptr++ = id >> 8;  // ID1
	*ptr++ = id >> 16; // ID2
	*ptr++ = id >> 24; // ID3

	*ptr++ = 0x00; // data len 低位在前高位在后
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x00; // len  4Bytes

	*ptr++ = subcmd; // 子消息类型
	*ptr++ = res;	 // 成功

	//		*ptr++ = 0x20;  //采集编号-0x20
	//		*ptr++ = 0x00;  //闪烁标记
	//		*ptr++ = 0xFF;
	//		*ptr++ = 0xFF;
	//		*ptr++ = 0x01;
	//		*ptr++ = 0x30;
	if (context != NULL && lenofcontext > 0)
	{ // 填充内容非空---

		for (i = 0; i < lenofcontext; i++) // 拷贝显示内容
		{
			*ptr++ = *(context + i);
		}
	}
	/*! 填充包尾*/
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	total_len = ptr - dst; // 总长度

	// 包总长度
	dst[4] = total_len & 0xff;
	dst[5] = (total_len >> 8) & 0xff;
	dst[6] = (total_len >> 16) & 0xff;
	dst[7] = (total_len >> 24) & 0xff;
	// 数据长度 = 包总长度-19
	total_len -= 19;
	dst[13] = total_len & 0xff;
	dst[14] = (total_len >> 8) & 0xff;
	dst[15] = (total_len >> 16) & 0xff;
	dst[16] = (total_len >> 24) & 0xff;

	return (total_len + 19);
}
/*******************************************************************************************************************
@funciton:  【函数名称】	uint16_t packQYLEDTextByRS232(uint8_t *dst,uint16_t lenofdst, uint8_t index,uint8_t *src,uint8_t lenofsrc)
@brief      【函数描述】	把字符串打包成QYLED串口协议格式

@param [in] 【参数描述】 dst 目标字符串 lenofdst 目标字符串总长度 index 对应的实时采集种类编号 src 要显示的字符串
						 lenofsrc 要显示的字符串长度
@return     【返回值】   uint16_t  长度为打包后的dst有效内容的总长度
@retval     【返回值】
@remarks    【注意事项】  lenofsrc 最大值为16，返回值最大值为300 并且2个数据包之间的时间间隔最少为10ms.
						  推荐使用50ms以确保LED控制卡识别出数据帧
@note       【注意事项】
@see        【其他参考项】
********************************************************************************************************************/
uint16_t packQYLEDText2Server(uint8_t *dst, uint16_t lenofdst, uint8_t index, uint8_t *src, uint8_t lenofsrc)
{
	uint8_t *ptr;
	uint8_t cplen, i;
	uint32_t total_len;
	// 参数检测
	if (dst == NULL || src == NULL || lenofdst < 23) // 参数不符合要求
		return 0x0;

	ptr = dst;
	*ptr++ = 0xfe; // 前导码
	*ptr++ = 0x5c;
	*ptr++ = 0x4b;
	*ptr++ = 0x89;
	*ptr++ = 0x00; // totle len frame len
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x00; // len  4Bytes 低位在前高位在后

	*ptr++ = 0x68; // CMD--实时采集命令---
	*ptr++ = 0x01; // ID0
	*ptr++ = 0x00; // ID1
	*ptr++ = 0x00; // ID2
	*ptr++ = 0x00; // ID3

	*ptr++ = 0x00; // data len 低位在前高位在后
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x00; // len  4Bytes

	*ptr++ = 0x10; // 子消息类型
	*ptr++ = 0x31; // 成功

	*ptr++ = 0x20; // 采集编号-0x20
	*ptr++ = 0x00; // 闪烁标记
	*ptr++ = 0xFF;
	*ptr++ = 0xFF;
	*ptr++ = 0x01;
	*ptr++ = 0x30;

	*ptr++ = index; // 采集编号
	*ptr++ = 0x00;	// 闪烁标记
	*ptr++ = 0xFF;	// 字符颜色 0xff 表示由上位机模板决定颜色-01-红色 02-绿色 03-黄色
	/*! 颜色解析
	2） 字符颜色： 字节高4 位表示数字颜色，低4 位表示后缀单位颜色。
	6
	01-红；
	02-绿；
	03-黄。
	若该字节=0xff 表示该处的颜色由内容编辑软件模板定义。
	*/
	*ptr++ = 0xFF; // 字体字号 高4bit是字体低4bit是字号
	/*! 字体字号解析
			字体和字号: 字节高4 位表示字体，低4 位表示字号。
			字体：（从1 开始）依次为：宋体、楷体、黑体、隶书、行书。注：C1、
			N1、C2、N2 控制卡支持宋体和黑体
			字号：（从0 开始）依次为：12*12、16×16、24×24，32×32、48×
			48，64×64，80×80，96×96。注：C1、N1、C2、N2 控制
			卡支持12*12、16x16 和32x32 点
			若该字节=0xff 表示该处的颜色由内容编辑软件模板定义。
	*/
	if (lenofsrc > 16)
		cplen = 16;
	else
		cplen = lenofsrc;
	*ptr++ = cplen; // 显示的长度 不能大于16字节

	for (i = 0; i < cplen; i++) // 拷贝显示内容
	{
		*ptr++ = *(src + i);
	}

	/*! 填充包尾*/
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	total_len = ptr - dst; // 总长度

	// 包总长度
	dst[4] = total_len & 0xff;
	dst[5] = (total_len >> 8) & 0xff;
	dst[6] = (total_len >> 16) & 0xff;
	dst[7] = (total_len >> 24) & 0xff;
	// 数据长度 = 包总长度-19
	total_len -= 19;
	dst[13] = total_len & 0xff;
	dst[14] = (total_len >> 8) & 0xff;
	dst[15] = (total_len >> 16) & 0xff;
	dst[16] = (total_len >> 24) & 0xff;

	return (total_len + 19);
}

/*******************************************************************************************************************
@funciton:  【函数名称】	uint16_t packQYLEDTextByRS232(uint8_t *dst,uint16_t lenofdst, uint8_t index,uint8_t *src,uint8_t lenofsrc)
@brief      【函数描述】	把字符串打包成QYLED串口协议格式

@param [in] 【参数描述】 dst 目标字符串 lenofdst 目标字符串总长度 index 对应的实时采集种类编号 src 要显示的字符串
						 lenofsrc 要显示的字符串长度
@return     【返回值】   uint16_t  长度为打包后的dst有效内容的总长度
@retval     【返回值】
@remarks    【注意事项】  lenofsrc 最大值为16，返回值最大值为300 并且2个数据包之间的时间间隔最少为10ms.
						  推荐使用50ms以确保LED控制卡识别出数据帧
@note       【注意事项】
@see        【其他参考项】
********************************************************************************************************************/
uint16_t packQYLEDTextByRS232(uint8_t *dst, uint16_t lenofdst, uint8_t index, uint8_t *src, uint8_t lenofsrc)
{
	uint8_t *ptr;
	uint8_t cplen, i;
	uint32_t total_len;
	// 参数检测
	if (dst == NULL || src == NULL || lenofdst < 23) // 参数不符合要求
		return 0x0;

	ptr = dst;
	*ptr++ = 0xfe; // 前导码
	*ptr++ = 0x5c;
	*ptr++ = 0x4b;
	*ptr++ = 0x89;
	*ptr++ = 0x00; // totle len frame len
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x00; // len  4Bytes 低位在前高位在后

	*ptr++ = 0x65; // CMD--实时采集命令---
	*ptr++ = 0x00; // ID0
	*ptr++ = 0x00; // ID1
	*ptr++ = 0x00; // ID2
	*ptr++ = 0x00; // ID3

	*ptr++ = 0x00; // data len 低位在前高位在后
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x00; // len  4Bytes

	*ptr++ = index; // 采集编号
	*ptr++ = 0x00;	// 闪烁标记
	*ptr++ = 0xFF;	// 字符颜色 0xff 表示由上位机模板决定颜色-01-红色 02-绿色 03-黄色
	/*! 颜色解析
	2） 字符颜色： 字节高4 位表示数字颜色，低4 位表示后缀单位颜色。
	6
	01-红；
	02-绿；
	03-黄。
	若该字节=0xff 表示该处的颜色由内容编辑软件模板定义。
	*/
	*ptr++ = 0xFF; // 字体字号 高4bit是字体低4bit是字号
	/*! 字体字号解析
			字体和字号: 字节高4 位表示字体，低4 位表示字号。
			字体：（从1 开始）依次为：宋体、楷体、黑体、隶书、行书。注：C1、
			N1、C2、N2 控制卡支持宋体和黑体
			字号：（从0 开始）依次为：12*12、16×16、24×24，32×32、48×
			48，64×64，80×80，96×96。注：C1、N1、C2、N2 控制
			卡支持12*12、16x16 和32x32 点
			若该字节=0xff 表示该处的颜色由内容编辑软件模板定义。
	*/
	if (lenofsrc > 16)
		cplen = 16;
	else
		cplen = lenofsrc;
	*ptr++ = cplen; // 显示的长度 不能大于16字节

	for (i = 0; i < cplen; i++) // 拷贝显示内容
	{
		*ptr++ = *(src + i);
	}

	/*! 填充包尾*/
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	total_len = ptr - dst; // 总长度

	// 包总长度
	dst[4] = total_len & 0xff;
	dst[5] = (total_len >> 8) & 0xff;
	dst[6] = (total_len >> 16) & 0xff;
	dst[7] = (total_len >> 24) & 0xff;
	// 数据长度 = 包总长度-19
	total_len -= 19;
	dst[13] = total_len & 0xff;
	dst[14] = (total_len >> 8) & 0xff;
	dst[15] = (total_len >> 16) & 0xff;
	dst[16] = (total_len >> 24) & 0xff;

	return (total_len + 19);
}
/*******************************************************************************************************************
@funciton:  【函数名称】	void TestLed(void)
@brief      【函数描述】	测试LED控制卡

@param [in] 【参数描述】

@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】

@note       【注意事项】
@see        【其他参考项】
********************************************************************************************************************/
void TestLed(void)
{

	static uint8_t buffer[256];
	uint8_t stringbuf[16];
	uint16_t len;
	static uint16_t counter;
	// 往41编号发送一个内容
	memset(stringbuf, 0, sizeof(stringbuf)); // 清除为0
	// sprintf((char *)stringbuf,"12345");
	sprintf((char *)stringbuf, "%d", counter++); // counter 每次调用++
	len = packQYLEDTextByRS232(buffer, sizeof(buffer), 41, stringbuf, strlen((char *)stringbuf));
	if (len > 0)
	{
		// send2Uart(buffer,len); //send2Uart需要自己实现
	}
}

/*******************************************************************************************************************
@funciton:  【函数名称】	uint8_t setRealData(uint8_t *dst,uint8_t Index,uint8_t Color,uint8_t Font,uint8_t Len)
@brief      【函数描述】	设置实时采集的属性

@param [in] 【参数描述】    dst 目标缓冲区 index 采集编号 Color颜色 Font字体字号 Len 内容长度
@return     【返回值】      0
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************************************************/
uint8_t setRealData(uint8_t *dst, uint8_t Index, uint8_t Color, uint8_t Font, uint8_t Len)
{
	REAL_DATA *pRealData_t;

	pRealData_t = (REAL_DATA *)(dst);
	pRealData_t->Index = Index;
	pRealData_t->Rsv = 0x0;
	pRealData_t->Color = Color;
	pRealData_t->Font = Font;
	pRealData_t->Len = Len;
	return (sizeof(REAL_DATA));
}
/*******************************************************************************
@funciton:  【函数名称】	uint8_t packSelectDisplayPage(uint8_t *src,uint8_t no)
@brief      【函数描述】	打包显示页

@param [in] 【参数描述】    src,源数据 no-显示页页序(1-255) 255表示退出点播
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint8_t packSelectDisplayPage(uint8_t *src, uint8_t no)
{
	*src++ = no;
	*src++ = 0xff - no;
	return 2;
}
/*******************************************************************************
@funciton:  【函数名称】	uint8_t pack_db_Context(uint8_t *dst,
													uint8_t area_no,
													uint16_t index_start,
													uint16_t numbers )
@brief      【函数描述】	打包点播内容/图片组

@param [in] 【参数描述】    area_no 区域0-255
							index_start 开始索引
							0-65536 numbers 图片/区域大小/个数
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint8_t pack_db_Context(uint8_t *dst, uint8_t area_no, uint16_t index_start, uint16_t numbers)
{
	DB_Context *pdbcon;
	// 修改db 内容
	pdbcon = (DB_Context *)(dst);
	pdbcon->AreaNumber = area_no;
	pdbcon->PicStart = ntohs(index_start);
	pdbcon->PicTotle = ntohs(numbers);
	pdbcon->PauseTime = 0;
	pdbcon->Speed = 0;
	pdbcon->DisplayMode = 0;

	return sizeof(DB_Context);
}
/*******************************************************************************
@funciton:  【函数名称】	uint8_t modify_db_Context_PalyMode(uint8_t *dst,uint8_t dispmode,uint8_t speed,uint8_t ptime)
@brief      【函数描述】

@param [in] 【参数描述】
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint8_t modify_db_Context_PalyMode(uint8_t *dst, uint8_t dispmode, uint8_t speed, uint8_t ptime)
{
	DB_Context *pdbcon;
	// 修改db 内容
	pdbcon = (DB_Context *)(dst);
	pdbcon->DisplayMode = dispmode;
	pdbcon->Speed = speed;
	pdbcon->PauseTime = ptime;
	return 0;
}
/*******************************************************************************
@funciton:  【函数名称】	uint8_t  pack_adj_brightness(uint8_t *ptr,uint8_t prio,uint8_t level)
@brief      【函数描述】	打包调整亮度

@param [in] 【参数描述】    ptr，目标缓冲 prio 优先级 level 亮度
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint8_t pack_adj_brightness(uint8_t *ptr, uint8_t prio, uint8_t level)
{
	*ptr++ = prio;
	*ptr++ = 0xff - prio; // 补码
	// 亮度等级 0-7    0-最亮  7-最暗
	*ptr++ = level;
	*ptr++ = 0xff - level; // 补码
	return 4;
}

// #define  TTS_BASE_ADDR  (SYSTEM_VIOCE_ADDR)
// #define  TTS_MAX_LEN    (SYSTEM_VIOCE_MAX)
/*******************************************************************************
@funciton:  【函数名称】
@brief      【函数描述】

@param [in] 【参数描述】
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint8_t *strstr_s(uint8_t *src, uint16_t maxlen, uint8_t *match, uint16_t match_len)
{
	uint16_t i, j;
	uint8_t *pret = NULL;
	for (i = 0, j = 0; i < maxlen; i++)
	{ //! --遍历字符串
		if (*(src + i) == *(match + j))
		{
			j++;
			if (j > match_len)
			{
				pret = src + i;
				break;
			}
		}
		else
		{
			i = i - j + 1;
			j = 0;
		}
	}
	return pret;
}
/*******************************************************************************
@funciton:  【函数名称】
@brief      【函数描述】

@param [in] 【参数描述】
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint8_t *strchr_s(uint8_t *src, uint16_t maxlen, uint8_t ch)
{
	uint8_t *pret = NULL;
	uint16_t i = 0;
	while (maxlen--)
	{
		if (src[i] == ch)
		{
			pret = (src + i);
			break;
		}
		i++;
	}

	return pret;
}
/*******************************************************************************
@funciton:  【函数名称】	uint16_t get_tts_by_index(uint8_t *dst,
													  uint16_t maxlen,
													  uint8_t *ret)
@brief      【函数描述】

@param [in] 【参数描述】
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint16_t get_tts_by_index(uint8_t *dst, uint16_t maxlen, uint8_t index, uint32_t *ret)
{
	uint8_t *pend, *pstart;
	uint16_t len = 0;
	// uint16_t maxlen;
	if (index > 128)
		return 0;
	pstart = NULL;
	ret = ret;
	// maxlen = TTS_MAX_LEN;
	while (index--)
	{
		if (pstart == NULL)
		{
			pstart = dst;
		}
		else
		{
			pstart = pend + 1;
		}
		// pend = (uint8_t *)strstr((char *)pstart,"\r\n");
		// pend = (uint8_t *)strchr((char *)pstart,'\n');
		pend = strchr_s(pstart, dst + maxlen - pstart, '\n');
		if (pend == NULL)
			break;
	}

	if (pend != NULL)
	{
		len = pend - pstart;
		*ret = (uint32_t)pstart;
	}
	return len;
}
/*******************************************************************************
@funciton:  【函数名称】	uint16_t pack_ttsbuffer_by_index(uint8_t *dst,
															 uint16_t maxlen,
															 uint8_t index)
@brief      【函数描述】

@param [in] 【参数描述】
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint16_t pack_ttsbuffer_by_index(uint32_t tts_base, uint16_t tts_max, uint8_t *dst, uint16_t maxlen, uint8_t index)
{
	uint32_t ret = 0;
	uint8_t *ptr;
	uint16_t len;
	len = get_tts_by_index((uint8_t *)tts_base, tts_max, index + 1, &ret);
	if (len == 0 || ret == 0)
		return 0;
	if (len > maxlen - 5)
		len = maxlen - 5;
	//! 需要添加FD 00 00 01 00 +内容
	ptr = dst;

	*ptr++ = 0xfd;
	*ptr++ = (len + 2) >> 8;
	*ptr++ = (len + 2);
	*ptr++ = 0x01; // 合成命令
	*ptr++ = 0x00; // 编码格式 UNICODE 03 00 GB2312 BGK 01 BIG5 02
	memcpy(ptr, (uint8_t *)ret, len);
	// ptr+=len;
	return (len + 5);
}
/*******************************************************************************
@funciton:  【函数名称】	uint16_t pack_tts_db(uint8_t *dst,uint8_t index,uint8_t addr)
@brief      【函数描述】	打包tts

@param [in] 【参数描述】
@return     【返回值】
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************/
uint16_t pack_tts_db(uint32_t tts_base, uint16_t tts_max, uint8_t *dst, uint8_t index, uint8_t addr)
{
	//! 查找地址 /r/n结束为一行
	uint32_t ret = 0;
	uint8_t *ptr;
	uint16_t len;
	len = get_tts_by_index((uint8_t *)tts_base, tts_max, index + 1, &ret);
	if (len == 0 || ret == 0)
		return 0;
	//! 符合组装---- ptr len
	ptr = dst;
	//! ptr-组装
	*ptr++ = 0xfe;
	*ptr++ = addr;
	/* 长度 */
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	/* 源地址*/
	*ptr++ = 0x97;
	//! 声音---
	*ptr++ = 0x73;
	// FD 00 0A 01 00 BB B6 D3 AD B9 E2 C1 D9
	*ptr++ = 0xfd;
	*ptr++ = (len + 2) >> 8;
	*ptr++ = (len + 2);
	*ptr++ = 0x01; // 合成命令
	*ptr++ = 0x00; // 编码格式 UNICODE 03 00 GB2312 BGK 01 BIG5 02
	memcpy(ptr, (uint8_t *)ret, len);
	ptr += len;

	/* 计算长度 */
	len = ptr - dst - 1;
	/* 填充长度 */
	dst[2] = (len >> 8);
	dst[3] = (len);
	/* 计算校验 */
	*ptr++ = check_sum(dst + 1, len);
	/* 需要加上头部以及尾部校验 */
	len += 2;

	return len;
}
/*******************************************************************************************************************
@funciton:  【函数名称】	uint16_t db_msg_RealDAQ(uint8_t *dst,uint8_t *src_con,uint16_t conlen)
@brief      【函数描述】	打包网络缓冲区/RS232 通讯数据包

@param [in] 【参数描述】    dst 目标缓冲区 cmd 命令字 id本次通讯ID，src_con 源文件 conlen 源文件长度
@return     【返回值】      返回整个数据包的长度
@retval     【返回值】
@remarks    【注意事项】
@note       【注意事项】
@see        【其他参考项】
********************************************************************************************************************/
uint16_t packNetBuffer(uint8_t *dst, uint8_t cmd, uint32_t id, uint8_t *src_con, uint16_t conlen)
{

	NetHead_T *phead_t;
	uint16_t len;
	uint8_t *ptr;

	phead_t = (NetHead_T *)dst;
	phead_t->PreCode = INT2BYTE(0x894b5cfe);
	phead_t->Command = cmd;
	phead_t->ID = INT2BYTE(id);

	ptr = dst + sizeof(NetHead_T);
	memcpy(ptr, src_con, conlen);
	ptr += conlen;

	// 后导码
	*ptr++ = 0xff;
	*ptr++ = 0xff;

	len = ptr - dst; // 数据帧总长度
	phead_t->Len = INT2BYTE(len);
	phead_t->CommLen = INT2BYTE(len - sizeof(NetHead_T) - 2);

	return len;
}
/*******************************************************************************************************************
@funciton:  【函数名称】	uint16_t pack485Buffer(uint8_t *dst,uint8_t *src_con,uint16_t conlen)
@brief      【函数描述】	打包485 通讯数据包

@param [in] 【参数描述】    dst 目标缓冲区 cmd 命令字 mac 内部码(三种形式 1：内部码 2：前面7个是0 第8个字节是地址 3：全部0 广播包)，
							src_con 源文件 conlen 源文件长度
@return     【返回值】      返回整个数据包的长度
@retval     【返回值】
@remarks    【注意事项】    必须保证dst mac src_con 为非空字符 mac必须有8字节空间
@note       【注意事项】
@see        【其他参考项】
********************************************************************************************************************/
uint16_t pack485Buffer(uint8_t *dst, uint8_t cmd, uint8_t *mac, uint8_t *src_con, uint16_t conlen)
{
	/*
	uint8_t PreCode;       //前导码--固定0xfe
	uint8_t DstAddr;        //目标地址
	uint16_t Len;           //命令总长度-----不包括前导码和校验字
	uint8_t SrcAddr;        //源地址
	uint8_t Command;        //命令字
	uint8_t Rsv1;           //保留字 填0
	uint8_t Mac[8];         //485物理地址---一共有三种形式 1：内部码 2：前面7个是0 第8个字节是地址 3：全部0 广播包
	*/
	uint8_t *ptr;
	uint16_t len;
	uint16_t i;

	ptr = dst;
	*ptr++ = 0xfe;
	*ptr++ = 0x98;
	/* 长度 */
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	/* 源地址*/
	*ptr++ = 0x97;
	/* 命令字*/
	*ptr++ = cmd;
	/* 保留 */
	*ptr++ = 0x00;
	/* mac地址*/
	if (mac == NULL)
	{
		for (i = 0; i < 8; i++)
			*ptr++ = 0x0;
	}
	else
	{
		for (i = 0; i < 8; i++)
			*ptr++ = *mac++;
	}
	/* 具体内容 */
	for (i = 0; i < conlen; i++)
		*ptr++ = *src_con++;

	/* 计算长度 */
	len = ptr - dst - 1;
	/* 填充长度 */
	dst[2] = (len >> 8);
	dst[3] = (len);
	/* 计算校验 */
	*ptr++ = check_sum(dst + 1, len);
	/* 需要加上头部以及尾部校验 */
	len += 2;

	return len;
}

/*
   CMD    Prio   Prio_C    Lv   Lv_C
  0x76   0-255   255-0     0-7  255-248

*/

/*******************************************************************************************************************
** 函数名称:     uint8 fillLevel(uint8 protoc,uint8 prio,uint8 level,uint8 *dst)
**
** 功能描述:
**
** 输　  入:
**
** 输 　 出:
**
** 返 回 值:
********************************************************************************************************************/
uint8_t fillLevel(uint8_t protoc, uint8_t prio, uint8_t level, uint8_t *dst)
{
	uint8_t *ptr;
	//	uint8_t i;
	uint32_t tmp;
	uint32_t len;
	ptr = dst;

	if (protoc == 0) // RS232  协议
	{
		*ptr++ = 0xfe;
		*ptr++ = 0x5c;
		*ptr++ = 0x4b;
		*ptr++ = 0x89;
		*ptr++ = 0x00; // totle len frame len
		*ptr++ = 0x00;
		*ptr++ = 0x00;
		*ptr++ = 0x00; // len  4Bytes

		*ptr++ = 0x76; // CMD

		*ptr++ = 0x01; // ID0
		*ptr++ = 0x02; // ID1
		*ptr++ = 0x03; // ID2
		*ptr++ = 0x04; // ID3

		*ptr++ = 0x00; // data len
		*ptr++ = 0x00;
		*ptr++ = 0x00;
		*ptr++ = 0x00; // len  4Bytes

		//	  if(c_b==COM_ARG)
		//	    {
		//         *ptr++ = 0x10;
		//         *ptr++ = 0x31;
		//		}
	}
	else
	{
		*ptr++ = 0xfe;
		*ptr++ = 0x98; // dst  address server
		*ptr++ = 0x0;  // Frame Len
		*ptr++ = 0x0;
		*ptr++ = 0x97; // SYS_ARG1.LOCAL_ADDR;   //本地地址
		*ptr++ = 0x76; // CMD --调整亮度
		*ptr++ = 0x00; // 保留
		*ptr++ = 0;	   // 0x05;  //LED_ID[0]; //显示屏8字节内部码。若8字节地址都为0x00,表示显示屏不需要关心内部码。
		*ptr++ = 0;	   // 0x06;  //LED_ID[1];
		*ptr++ = 0;	   // 0x00;  //LED_ID[2];
		*ptr++ = 0;	   // 0x06;  //LED_ID[3];
		*ptr++ = 0;	   // 0x08;  //LED_ID[4];
		*ptr++ = 0;	   // 0x05;  //LED_ID[5];
		*ptr++ = 0;	   // 0x24;  //LED_ID[6];
		*ptr++ = 0;	   // 0x98;  //LED_ID[7];
	}

	// 优先级---
	//          0      --->说明放弃当前命令，恢复到控制卡本身的亮度控制
	//          other  --->越高优先级越高
	*ptr++ = prio;
	*ptr++ = 0xff - prio; // 补码
	// 亮度等级 0-7    0-最亮  7-最暗
	*ptr++ = level;
	*ptr++ = 0xff - level; // 补码

	// 包结束，填包数据
	len = ptr - dst; // 计算出总长度

	if (protoc == 0) // RS232  协议
	{
		len += 2;
		// 对于
		*(dst + 4) = len & 0xff;
		*(dst + 5) = (len >> 8) & 0xff;
		*(dst + 6) = (len >> 16) & 0xff;
		*(dst + 7) = (len >> 24) & 0xff;

		tmp = len - 19; /// 数据长度
		*(dst + 13) = tmp & 0xff;
		*(dst + 14) = (tmp >> 8) & 0xff;
		*(dst + 15) = (tmp >> 16) & 0xff;
		*(dst + 16) = (tmp >> 24) & 0xff;

		*ptr++ = 0xff;
		*ptr++ = 0xff;
	}
	else
	{
		len--;
		*(dst + 2) = (len >> 8) & 0xff; // len
		*(dst + 3) = len & 0xff;
		tmp = check_sum(dst + 1, len);
		*ptr = (tmp & 0xff);
		len += 2;
	}

	return len;
}

/*******************************************************************************************************************
**
**          End of file
**
*******************************************************************************************************************/
