#ifndef __DMA_PHYSICAL_MEM_H
#define __DMA_PHYSICAL_MEM_H

#include <linux/list.h>
#include <linux/spinlock.h>

typedef struct {
    unsigned long   total_pages; 
    unsigned long   alloc_pages; 
    unsigned long   free_pages;
    unsigned long   page_size;
} DmaMemInfo_t;


typedef struct {
    unsigned long key[2];
} DmaMemKey_t;


typedef enum {
    LEFT,
    RIGHT
} DmaRotate_t;


typedef struct {
    int             pageno;
    unsigned long   addr;
    unsigned char  *kaddr;
    int             used;
    int             alloc_pages;
    int             first_pageno;
} DmaPage_t;

typedef struct avl_node_struct{
    struct list_head           ListEntry;
    DmaMemKey_t                key;
    int                        height;
    DmaPage_t*                 page;
    struct avl_node_struct*    left;
    struct avl_node_struct*    right;
} avl_node_t;

typedef struct {
    avl_node_t*             free_tree;
    avl_node_t*             alloc_tree;
    avl_node_t*             node_list;
    struct list_head        node_Free;
//	spinlock_t              node_Lock;
    DmaPage_t*              page_list;
    int                     num_pages;
    unsigned long           base_addr;
    unsigned long           mem_size;
    unsigned long           page_size;
    int                     free_page_count;
    int                     alloc_page_count;
    int                     usedcount;
} DmaMem_t;



int DmaMem_init(DmaMem_t* mm, unsigned long addr, unsigned long size, unsigned long pageSize);

int DmaMem_exit(DmaMem_t* mm);

unsigned long DmaMem_alloc(DmaMem_t* mm, int size);

int DmaMem_free(DmaMem_t* mm, unsigned long ptr);

int DmaMem_get_info(DmaMem_t* mm, DmaMemInfo_t* info);

#endif

