#include "DmaMem.h"
#include <linux/slab.h>
#include <linux/io.h>

#define HEIGHT(_tree)       (_tree==NULL ? -1 : _tree->height)
#define MAX(_a, _b)         (_a >= _b ? _a : _b)

static avl_node_t* DmaMem_popfront(DmaMem_t *mm);
static void DmaMem_pushback(DmaMem_t *mm, avl_node_t* node);


static DmaMemKey_t MAKE_KEY(unsigned long key0, unsigned long key1) {
    DmaMemKey_t vKey;
    vKey.key[0] = key0;
    vKey.key[1] = key1;
    return vKey;
}

static unsigned long KEY_TO_VALUE(DmaMemKey_t vKey) {
    return vKey.key[0];
}

static avl_node_t* make_avl_node(DmaMem_t* mm, DmaMemKey_t key,  DmaPage_t* page) {
    avl_node_t* node = (avl_node_t*)DmaMem_popfront(mm);
    if ( node == NULL ) {
        printk("[VDI] failed to allocate memory to make_avl_node\n");
        return NULL;
    }
    node->key     = key;
    node->page    = page;
    node->height  = 0;
    node->left    = NULL;
    node->right   = NULL;
    return node;
}

static int get_balance_factor(avl_node_t* tree) {
    int factor = 0;
    if (tree) {
        factor = HEIGHT(tree->right) - HEIGHT(tree->left);
    }

    return factor;
}

/*
* Left Rotation
*
*      A                      B
*       \                    / \
*        B         =>       A   C
*       /  \                 \
*      D    C                 D
*
*/
static avl_node_t* rotation_left(avl_node_t* tree) {
    avl_node_t* rchild;
    avl_node_t* lchild;
    if (tree == NULL) {
        return NULL;
    }

    rchild = tree->right;
    if (rchild == NULL) {
        return tree;
    }

    lchild = rchild->left;
    rchild->left = tree;
    tree->right = lchild;

    tree->height   = MAX(HEIGHT(tree->left), HEIGHT(tree->right)) + 1;
    rchild->height = MAX(HEIGHT(rchild->left), HEIGHT(rchild->right)) + 1;
    return rchild;
}

/*
* Reft Rotation
*
*         A                  B
*       \                  /  \
*      B         =>       D    A
*    /  \                     /
*   D    C                   C
*
*/
static avl_node_t* rotation_right(avl_node_t* tree) {
    avl_node_t* rchild;
    avl_node_t* lchild;
    if (tree == NULL) {
        return NULL;
    }

    lchild = tree->left;
    if (lchild == NULL) {
        return NULL;
    }

    rchild = lchild->right;
    lchild->right = tree;
    tree->left = rchild;

    tree->height   = MAX(HEIGHT(tree->left), HEIGHT(tree->right)) + 1;
    lchild->height = MAX(HEIGHT(lchild->left), HEIGHT(lchild->right)) + 1;

    return lchild;
}

static avl_node_t* do_balance(avl_node_t* tree) {
    int bfactor = 0, child_bfactor;       /* balancing factor */
    bfactor = get_balance_factor(tree);
    if (bfactor >= 2) {
        child_bfactor = get_balance_factor(tree->right);
        if (child_bfactor == 1 || child_bfactor == 0) {
            tree = rotation_left(tree);
        } else if (child_bfactor == -1) {
            tree->right = rotation_right(tree->right);
            tree        = rotation_left(tree);
        } else {
            printk("invalid balancing factor: %d\n", child_bfactor);
            return NULL;
        }
    } else if (bfactor <= -2) {
        child_bfactor = get_balance_factor(tree->left);
        if (child_bfactor == -1 || child_bfactor == 0) {
            tree = rotation_right(tree);
        } else if (child_bfactor == 1) {
            tree->left = rotation_left(tree->left);
            tree       = rotation_right(tree);
        } else {
            printk("invalid balancing factor: %d\n", child_bfactor);
            return NULL;
        }
    }

    return tree;
}

static avl_node_t* unlink_end_node(avl_node_t* tree, int dir, avl_node_t** found_node) {
    *found_node = NULL;
    if (tree == NULL) {
        return NULL;
    }

    if (dir == LEFT) {
        if (tree->left == NULL) {
            *found_node = tree;
            return NULL;
        }
    } else {
        if (tree->right == NULL) {
            *found_node = tree;
            return NULL;
        }
    }

    if (dir == LEFT) {
        tree->left = unlink_end_node(tree->left, LEFT, found_node);
        if (tree->left == NULL) {
            tree->left = (*found_node)->right;
            (*found_node)->left  = NULL;
            (*found_node)->right = NULL;
        }
    } else {
        tree->right = unlink_end_node(tree->right, RIGHT, found_node);
        if (tree->right == NULL) {
            tree->right = (*found_node)->left;
            (*found_node)->left  = NULL;
            (*found_node)->right = NULL;
        }
    }

    tree->height = MAX(HEIGHT(tree->left), HEIGHT(tree->right)) + 1;
    return do_balance(tree);
}

static avl_node_t* avltree_insert(DmaMem_t* mm, avl_node_t* tree, DmaMemKey_t key, DmaPage_t* page) {
    if (tree == NULL) {
        tree = make_avl_node(mm, key, page);
    } else {
          if ((key.key[0] > tree->key.key[0]) || ((key.key[0] == tree->key.key[0]) && (key.key[1] >= tree->key.key[1]))) {
            tree->right = avltree_insert(mm, tree->right, key, page);
        } else {
            tree->left  = avltree_insert(mm, tree->left, key, page);
        }
    }

    tree = do_balance(tree);
    tree->height = MAX(HEIGHT(tree->left), HEIGHT(tree->right)) + 1;
    return tree;
}

static avl_node_t* do_unlink(avl_node_t* tree) {
    avl_node_t* node;
    avl_node_t* end_node;
    node = unlink_end_node(tree->right, LEFT, &end_node);
    if (node) {
        tree->right = node;
    } else {
        node = unlink_end_node(tree->left, RIGHT, &end_node);
        if (node) tree->left = node;
    }

    if (node == NULL) {
        node = tree->right ? tree->right : tree->left;
        end_node = node;
    }

    if (end_node) {
        end_node->left  = (tree->left != end_node) ? tree->left : end_node->left;
        end_node->right = (tree->right != end_node) ? tree->right : end_node->right;
        end_node->height = MAX(HEIGHT(end_node->left), HEIGHT(end_node->right)) + 1;
    } 

    tree = end_node;
    return tree;
}

static avl_node_t* avltree_remove(avl_node_t* tree, avl_node_t** found_node, DmaMemKey_t key) {
    *found_node = NULL;
    if (tree == NULL) {
        printk("failed to find key %lu %lu\n", key.key[0], key.key[1]);
        return NULL;
    } 

    if (key.key[0] == tree->key.key[0]) {
        if(key.key[1] == tree->key.key[1]) {
            *found_node = tree;
            tree = do_unlink(tree);
        } else if (key.key[1] > tree->key.key[1]) {
            tree->right = avltree_remove(tree->right, found_node, key);
        } else {
            tree->left  = avltree_remove(tree->left, found_node, key);
        }
    } else if (key.key[0] > tree->key.key[0]) {
        tree->right = avltree_remove(tree->right, found_node, key);
    } else {
        tree->left  = avltree_remove(tree->left, found_node, key);
    }

    if (tree) tree->height = MAX(HEIGHT(tree->left), HEIGHT(tree->right)) + 1;

    tree = do_balance(tree);
    return tree;
}

static avl_node_t* remove_approx_value(avl_node_t* tree, avl_node_t** found, DmaMemKey_t key) {
    *found = NULL;
    if (tree == NULL) {
        return NULL;
    }

    if (key.key[0] == tree->key.key[0]) {
        *found = tree;
        tree = do_unlink(tree);
    } else if (key.key[0] > tree->key.key[0]) {
        tree->right = remove_approx_value(tree->right, found, key);
    } else {
        tree->left  = remove_approx_value(tree->left, found, key);
        if (*found == NULL) {
            *found = tree;
            tree = do_unlink(tree);
        }
    }
    if (tree) tree->height = MAX(HEIGHT(tree->left), HEIGHT(tree->right)) + 1;
    tree = do_balance(tree);
    return tree;
}

static void set_pages_free(DmaMem_t *mm, int pageno, int npages) {
    int last_pageno     = pageno + npages - 1;
    int i;
    DmaPage_t  *page, *last_page;

    if (last_pageno >= mm->num_pages) {
        printk("set_pages_free: invalid last page number: %d\n", last_pageno);
        return;
    }

    for (i = pageno; i <= last_pageno; ++i) {
        mm->page_list[i].used         =  0;
        mm->page_list[i].alloc_pages  =  0;
        mm->page_list[i].first_pageno = -1;
    }
}

static void set_blocks_free(DmaMem_t *mm, int pageno, int npages) {
    int last_pageno     = pageno + npages - 1;
    int i;
    DmaPage_t  *page, *last_page;
    if (last_pageno >= mm->num_pages) {
        printk("set_blocks_free: invalid last page number: %d\n", last_pageno);
        return;
    }

    page                 =  &mm->page_list[pageno];
    page->used           =  0;
    page->first_pageno   = -1;
    page->alloc_pages    = npages;

    last_page            = &mm->page_list[last_pageno];
    last_page->used      = 0;
    last_page->first_pageno = pageno;
    mm->free_tree = avltree_insert(mm, mm->free_tree, MAKE_KEY(npages, pageno), page); /*lint !e571 Suspicious cast*/
}

static void set_blocks_alloc(DmaMem_t *mm, int pageno, int npages) {
    int last_pageno     = pageno + npages - 1;
    int i;
    DmaPage_t  *page, *last_page;
    if (last_pageno >= mm->num_pages) {
        printk("set_blocks_free: invalid last page number: %d\n", last_pageno);
        return;
    }

    page                 = &mm->page_list[pageno];
    page->used           =  1;
    page->first_pageno   = -1;
    page->alloc_pages    = npages;

    last_page            = &mm->page_list[last_pageno];
    last_page->used      = 1;
    last_page->first_pageno = pageno;
    mm->alloc_tree = avltree_insert(mm, mm->alloc_tree, MAKE_KEY(page->addr, 0), page);
}

avl_node_t* DmaMem_popfront(DmaMem_t *mm) {
	avl_node_t* node = NULL;
    struct list_head    *pEntry;
//    spin_lock(&(mm->node_Lock));
    pEntry = mm->node_Free.next;

    // Traverse list to find the desired list object
    while (pEntry != &(mm->node_Free)) {
        // Get the object
        node = list_entry(pEntry, avl_node_t, ListEntry);
        // Remove the object from the list
        list_del(pEntry);
//       spin_unlock(&(mm->node_Lock));
        return node;// Jump to next item in the list
    }
//    spin_unlock(&(mm->node_Lock));
    return node;
}

void DmaMem_pushback(DmaMem_t *mm, avl_node_t* node) {
 //   spin_lock(&(mm->node_Lock));
    list_add_tail(&(node->ListEntry), &(mm->node_Free));
 //   spin_unlock(&(mm->node_Lock));
}


static void avltree_free(DmaMem_t *mm, avl_node_t* tree) {
    if ((mm == NULL) || (tree == NULL)) {
        return;
    }

    if (tree->left == NULL && tree->right == NULL) {
        DmaMem_pushback(mm, tree);
        return;
    }

    avltree_free(mm, tree->left);
    tree->left = NULL;
    avltree_free(mm, tree->right);
    tree->right = NULL;
    DmaMem_pushback(mm, tree);
}

int DmaMem_init(DmaMem_t* mm, unsigned long addr, unsigned long size, unsigned long pageSize) {
    int i;
    const unsigned long VMEM_PAGE_SIZE = pageSize;
    mm->base_addr  = (addr + (VMEM_PAGE_SIZE - 1)) & (~(VMEM_PAGE_SIZE - 1));
    mm->mem_size   = size & (~VMEM_PAGE_SIZE);
    mm->page_size  = pageSize;
    mm->num_pages  = mm->mem_size / VMEM_PAGE_SIZE;
    mm->page_list  = (DmaPage_t*)kmalloc(mm->num_pages * sizeof(DmaPage_t), GFP_KERNEL);
    if (mm->page_list == NULL) {
        printk("[VDI] failed to allocate when vmem_init\n");
        return -1;
    }
    mm->node_list  = (avl_node_t*)kmalloc(mm->num_pages * sizeof(avl_node_t), GFP_KERNEL);
    if (mm->node_list == NULL) {
        printk("[VDI] failed to allocate when vmem_init\n");
        kfree(mm->page_list);
        return -1;
    }
    memset(mm->node_list, 0, mm->num_pages * sizeof(avl_node_t));
    mm->free_tree  = NULL;
    mm->alloc_tree = NULL;
    mm->free_page_count = mm->num_pages;
    mm->alloc_page_count = 0;
    //printf("[VDI] vmem_init address %p, size %lx, pages %d\n", mm->base_addr, mm->mem_size, mm->num_pages);
//    spin_lock_init(&(mm->node_Lock));
    INIT_LIST_HEAD( &(mm->node_Free));
    for (i = 0; i < mm->num_pages; ++i) {
        mm->page_list[i].pageno       = i;
        mm->page_list[i].addr         = mm->base_addr + i * VMEM_PAGE_SIZE;
        mm->page_list[i].kaddr        = 0;
        mm->page_list[i].alloc_pages  = 0;
        mm->page_list[i].used         = 0;
        mm->page_list[i].first_pageno = -1;
        list_add_tail(&(mm->node_list[i].ListEntry), &(mm->node_Free));
    }

    set_blocks_free(mm, 0, mm->num_pages);
    return 0;
}

int DmaMem_exit(DmaMem_t* mm) {
    if (mm == NULL) {
        printk("vmem_exit: invalid handle\n");
        return -1;
    }

    if (mm->free_tree) {
        avltree_free(mm, mm->free_tree);
        mm->free_tree = NULL;
    }
    if (mm->alloc_tree) {
        avltree_free(mm, mm->alloc_tree);
        mm->alloc_tree = NULL;
    }

    if (mm->page_list) {
        kfree(mm->page_list);
        mm->page_list = NULL;
    }

    if (mm->node_list) {
        kfree(mm->node_list);
        mm->node_list = NULL;
    }

//   spin_lock(&(mm->node_Lock));
    INIT_LIST_HEAD( &(mm->node_Free));
//    spin_unlock(&(mm->node_Lock));
    return 0;
}

unsigned long DmaMem_alloc(DmaMem_t* mm, int size) {
    avl_node_t* node;
    DmaPage_t*  free_page;
    int         npages, free_npages;
    int         alloc_pageno;
    unsigned long  ptr;
    const unsigned long VMEM_PAGE_SIZE = mm->page_size;
    if (mm == NULL) {
    	printk("vmem_alloc: invalid handle\n");
        return (unsigned long)-1;
    }

    if (size <= 0) {
        printk("%d size of vmem_alloc, failed\n", size);
        return (unsigned long)-1;
    }

    npages = (size + VMEM_PAGE_SIZE -1)/VMEM_PAGE_SIZE;
    mm->free_tree = remove_approx_value(mm->free_tree, &node, MAKE_KEY(npages, 0)); /*lint !e571 Suspicious cast*/
    if (node == NULL) {
        printk("pages all:%d used:%d free:%d mm->free_tree:%p\n" , mm->num_pages, mm->alloc_page_count, mm->free_page_count, mm->free_tree);
        return (unsigned long)-1;
    }
    free_page = node->page;
    free_npages = KEY_TO_VALUE(node->key);
    DmaMem_pushback(mm, node);

    alloc_pageno = free_page->pageno;
    set_blocks_alloc(mm, alloc_pageno, npages);
    if (npages < free_npages) {
        int free_pageno = alloc_pageno + npages;
        set_blocks_free(mm, free_pageno, (free_npages - npages));
    }

    ptr = free_page->addr;
    mm->page_list[alloc_pageno].kaddr = memremap(ptr, size, MEMREMAP_WB);
    mm->alloc_page_count += npages;
    mm->free_page_count  -= npages;
    return ptr;
}

int DmaMem_free(DmaMem_t* mm, unsigned long ptr) {
    unsigned long addr;
    avl_node_t* found;
    DmaPage_t*  page;
    int pageno, last_pageno = -1;
    int merge_page_no, merge_page_size, free_page_size;

    if (mm == NULL) {
	    printk("vmem_free: invalid handle\n");
        return -1;
    }

    addr = ptr;
    mm->alloc_tree = avltree_remove(mm->alloc_tree, &found, MAKE_KEY(addr, 0));
    if (found == NULL) {
        printk("vmem_free: 0x%08x not found\n", addr);
        return -1;
    }

    /* find previous free block */
    page = found->page;
    DmaMem_pushback(mm, found);
    iounmap(page->kaddr);
    page->kaddr = 0;
    pageno = page->pageno;
    free_page_size = page->alloc_pages;

    /* find previous free block */
    {
        int prev_free_pageno = pageno - 1;
        int prev_size = -1;
        if (prev_free_pageno >= 0) {
            if (mm->page_list[prev_free_pageno].used == 0) {
                prev_free_pageno = mm->page_list[prev_free_pageno].first_pageno;
                if (prev_free_pageno >= 0) {
                   prev_size = mm->page_list[prev_free_pageno].alloc_pages;
                }
            }
        }

        /* merge */
        merge_page_no = page->pageno;
        merge_page_size = page->alloc_pages;
        if (prev_size > 0) {
            mm->free_tree = avltree_remove(mm->free_tree, &found, MAKE_KEY(prev_size, prev_free_pageno)); /*lint !e571 Suspicious cast*/
            if (found == NULL) {
                printk("vmem_free prev: %d %d not found\n", prev_size, prev_free_pageno);
                return -1;
            }
            merge_page_no    = found->page->pageno;
            merge_page_size += found->page->alloc_pages;
            found->page->alloc_pages  =   0;
            found->page->first_pageno =  -1;
            mm->page_list[pageno - 1].first_pageno = -1;
            DmaMem_pushback(mm, found);
        }
    }

   /* find next free block */
   {
        int next_free_pageno = pageno + page->alloc_pages;
        int next_size = -1;
        next_free_pageno = (next_free_pageno >= mm->num_pages) ? -1 : next_free_pageno;
        if (next_free_pageno >= 0) {
            if (mm->page_list[next_free_pageno].used == 0) {
                next_size = mm->page_list[next_free_pageno].alloc_pages;
            }
        }
        /* merge */
        if (next_size > 0) {
            mm->free_tree = avltree_remove(mm->free_tree, &found, MAKE_KEY(next_size, next_free_pageno)); /*lint !e571 Suspicious cast*/
            if (found == NULL) {
                printk("vmem_free next: %d %d not found\n", next_size, next_free_pageno);
                return -1;
            }
            merge_page_size += found->page->alloc_pages;
            last_pageno      = found->page->pageno + found->page->alloc_pages - 1;
            found->page->alloc_pages  =   0;
            found->page->first_pageno =  -1;
            if (last_pageno < mm->num_pages) {
                mm->page_list[last_pageno].first_pageno = -1;
            }
            DmaMem_pushback(mm, found);
        }
    }


    page->used        = 0;
    page->alloc_pages = 0;
    last_pageno       = page->pageno + page->alloc_pages - 1;
    if (last_pageno < mm->num_pages) {
        mm->page_list[last_pageno].used         = 0;
        mm->page_list[last_pageno].alloc_pages  = 0;
        mm->page_list[last_pageno].first_pageno = -1;
    }
    set_blocks_free(mm, merge_page_no, merge_page_size);

    mm->alloc_page_count -= free_page_size;
    mm->free_page_count  += free_page_size;
    //printk("FREE: total(%d) alloc(%d) free(%d)\n", mm->num_pages, mm->alloc_page_count, mm->free_page_count);
    return 0;
}

int DmaMem_get_info(DmaMem_t* mm, DmaMemInfo_t* info) {
    int i, pos = 0;
    if ((mm == NULL) || (info == NULL)) {
		//printk("vmem_get_info: invalid handle\n");
        return -1;
    }

    info->total_pages = mm->num_pages;
    info->alloc_pages = mm->alloc_page_count;
    info->free_pages  = mm->free_page_count;
    info->page_size   = mm->page_size;
    printk("FREE: total(%d) alloc(%d) free(%d), page_size: %d =====================\n", mm->num_pages, mm->alloc_page_count, mm->free_page_count, info->page_size);
    return 0;
}


