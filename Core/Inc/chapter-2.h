/**
  ******************************************************************************
  * @file    chapter-2.h
  * @brief   第 2 章：程序入口。这里只放 1.8 节的主函数（chapter_main）。
  ******************************************************************************
  */

#ifndef __CHAPTER_2_H__
#define __CHAPTER_2_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  本章的程序入口：调用 chapter-1 中的函数完成初始化与主循环。
  * @note   由 main() 在 CubeMX 外设初始化完成后调用，内部为死循环。
  */
void chapter_main(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHAPTER_2_H__ */
