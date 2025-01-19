


/******************************************************************************************************************
 *                                                      MM.C                                                      *
 *                                                NAME: [YUE WANG]                                                *
 * IN MY IMPLEMENTATION OF THE DYNAMIC MEMORY ALLOCATOR, I USED AN EXPLICIT FREE LIST TO MANAGE THE FREE BLOCKS.  *
 *   THE FREE LIST IS AN ARRAY OF POINTERS TO THE FIRST FREE BLOCK IN EACH SIZE CLASS. FOOTER OPTIMIZATION(ONLY   *
 * FREE BLOCK HAVE BOTH HEADER AND FOOTER) IS USED TO INCREASE THE UTILIZATION OF THE HEAP. THE MAIN CHALLANGE IN *
 *  FOOTER OPTIMIZATION IS TO KEEP THE SIZE OF THE BLOCK CONSISTENT IN THE HEADER AND FOOTER. MOREOVER, CHECKING  *
 *  IF THE LAST BLOCK IS FREE FOR MARKING THE NEW ALLOCATED BLOCK'S PREV_IS_FREE BIT IS ALSO A CHALLENGE. I USED  *
 *                    THE LAST BIT OF THE HEADER TO STORE WHETHER THE PREVIOUS BLOCK IS FREE.                     *
 ******************************************************************************************************************/
 
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

/*
 * If you want to enable your debugging output and heap checker code,
 * uncomment the following line. Be sure not to have debugging enabled
 * in your final submission.
 */
// #define DEBUG

#ifdef DEBUG
// When debugging is enabled, the underlying functions get called
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#else
// When debugging is disabled, no code gets generated
#define dbg_printf(...)
#define dbg_assert(...)
#endif // DEBUG

// do not change the following!
#ifdef DRIVER
// create aliases for driver tests
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mm_memset
#define memcpy mm_memcpy
#endif // DRIVER

#define ALIGNMENT 16
//#####################################
// defining some constants
#define headerORFooter_SIZE 8
#define prev_SIZE 8
#define next_SIZE 8
#define BLK_NUM_INIT 4
#define INIT_SIZE 32    //Padding(8) + Prologue(16) + Epilogue(8)
#define NUM_FREE_LISTS 14
//#####################################

void* free_lists[NUM_FREE_LISTS];
void* last_block_start;
int heap_size = 0;

                                            /*****************************
                                             * START OF HELPER FUNCTIONS *
                                             *****************************/

/***************************************************************************************
 *                              LIST OF HELPER FUNCTIONS:                              *
 *           1. ALIGN(NOT USED IN MY IMPLEMENTATION BUT KEPT FOR REFERENCE)            *
 *              2. ALIGNX: ALIGN THE SIZE OF THE BLOCK TO BE 24 + N * 16               *
 *             3. SET: SET THE VALUE OF A 64-BIT INTEGER AT THE ADDRESS A              *
 *                4. SETP: SET THE VALUE OF A POINTER AT THE ADDRESS A                 *
 * 5. EXTRACT_LAST_BIT(NOT USED BUT KEPT FOR REFERENCE): GET THE VALUE OF THE LAST BIT *
 *           6. EXTRACT_SIZE: GET THE VALUE OF THE SIZE OF THE CURRENT BLOCK           *
 *           7. EXTRACT_PREV_IS_FREE: GET THE VALUE OF THE BLOCK'S FREE BIT            *
 *       8. EXTRACT_CURR_IS_FREE: GET THE VALUE OF THE BLOCK'S PREVIOUS FREE BIT       *
 *          9. EXTRACT_NEXT_FREE_BLK: GET THE ADDRESS OF THE NEXT FREE BLOCK           *
 *       10. GET_FREE_LIST_INDEX: RETURN WHICH FREE LIST THE BLOCK SHOULD BE IN        *
 *            11. ADD_TO_FREE_LIST: ADD THE BLOCK TO THE DOUBLE LINKED LIST            *
 *       12. REMOVE_FROM_FREE_LIST: REMOVE THE BLOCK FROM THE DOUBLE LINKED LIST       *
 * 13. COALESCE: COALESCE THE BLOCK WITH THE PREVIOUS AND NEXT BLOCKS IF THEY ARE FREE *
 ***************************************************************************************/

/**************************************************
 *             HELPER FUNCTION: ALIGN             *
 *                    NOT USED                    *
 * ROUNDS UP TO THE NEAREST MULTIPLE OF ALIGNMENT *
 **************************************************/
static size_t align(size_t x)
{
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

/***********************************************************************************************
 *                                   HELPER FUNCTION: ALIGN                                    *
 *                   AFTER FOOTER OPTIMIZATION, SINCE THERE IS NO FOOTER IN                    *
 *                 A ALLOCATED BLOCK, THIS EXTRA 8 BYTES CAN BE USED AS A PART                 *
 *               OF THE PAYLOAD. TO ENSURE THAT THE PAYLOAD IS 16-BYTE ALIGNED,                *
 *           WE NEED TO ADJUST THE SIZE OF THE BLOCK TO BE 24 + N * MULTIPLE OF 16.            *
 *      ALSO, THE SIZE OF THE BLOCK SHOULD BE AT LEAST 24 BYTES SINCE A FREE BLOCK SHOULD      *
 * AT LEAST HAVE A HEADER, A FOOTER, A PREV POINTER AND A NEXT POINTER, WHICH IS 8 BYTES EACH. *
 ***********************************************************************************************/

static size_t alignx(size_t x)
{
    if ((8 + 16 * ((x + 15 - 8) / 16))<24){
        return 24;
    } 
    return 8 + 16 * ((x + 15 - 8) / 16);
}

/******************************************************
 * SET THE VALUE OF A 64-BIT INTEGER AT THE ADDRESS A *
 ******************************************************/
static void set(void *a, uint64_t v) {
    *(uint64_t *)a = v;
}

/***********************************************
 * SET THE VALUE OF A POINTER AT THE ADDRESS A *
 ***********************************************/
static void setp(void* a, void* v) {
    *(void**)a = v;
}

/******************************
 * GET THE VALUE THE LAST BIT *
 ******************************/
static int extract_last_bit(void* ptr) {
    uint8_t value = *(uint8_t*)ptr;
    return value & 0x01;
}

/**************************************************
 * GET THE VALUE OF THE SIZE OF THE CURRENT BLOCK *
 **************************************************/
static size_t extract_size(void* ptr) {
    size_t value = *(size_t*)ptr;
    return value >> 2;
}

/*****************************************
 * GET THE VALUE OF THE BLOCK'S FREE BIT *
 *   RETURN 1 IF ALLOCATED, 0 IF FREE    *
 *****************************************/
static int extract_prev_is_free(void* ptr) {
    if (ptr == mm_heap_lo() + 3 * headerORFooter_SIZE) return 1; // prologue is not free
    uint8_t value = *(uint8_t*)ptr;
    return value & 0x01;
}

/**************************************************
 * GET THE VALUE OF THE BLOCK'S PREVIOUS FREE BIT *
 *        RETURN 1 IF ALLOCATED, 0 IF FREE        *
 **************************************************/
static int extract_curr_is_free(void* ptr) {
    uint8_t value = *(uint8_t*)ptr;
    return (value & 0x02) >> 1;
}

/*****************************************
 * GET THE ADDRESS OF THE NEXT FREE BLK. *
 *****************************************/
static int* extract_next_free_blk(void* ptr) {
    int* value = *(int**)((char*)ptr + headerORFooter_SIZE + prev_SIZE);
    return value;
}

/*************************************************
 *     HELPER FUNCTION: GET_FREE_LIST_INDEX      *
 * RETURN WHICH FREE LIST THE BLOCK SHOULD BE IN *
 *************************************************/
static int get_free_list_index(size_t size) {
    if (size == 1) return 0;
    if (size == 2) return 1;
    if (size >= 3 && size <= 4) return 2;
    if (size >= 5 && size <= 8) return 3;
    if (size >= 9 && size <= 16) return 4;
    if (size >= 17 && size <= 32) return 5;
    if (size >= 33 && size <= 64) return 6;
    if (size >= 65 && size <= 128) return 7;
    if (size >= 129 && size <= 256) return 8;
    if (size >= 257 && size <= 512) return 9;
    if (size >= 513 && size <= 1024) return 10;
    if (size >= 1025 && size <= 2048) return 11;
    if (size >= 2049 && size <= 4096) return 12;
    return 13; // size is larger than 4096
}

/*******************************************
 *    HELPER FUNCTION: ADD_TO_FREE_LIST    *
 * ADD THE BLOCK TO THE DOUBLE LINKED LIST *
 *******************************************/
static void add_to_free_list(void *block)
{
    size_t size = extract_size(block);
    int index = get_free_list_index(size);

    void **free_list_head = &free_lists[index];

    // add block to the head of the free list
    if (*free_list_head != NULL)
    {
        // the free list is not empty
        // next ptr of the new head
        setp(block + headerORFooter_SIZE + prev_SIZE, *free_list_head);
        // set the previous pointer of the current head to the new block
        setp((char *)*free_list_head + headerORFooter_SIZE, block);
        // set the previous pointer of the block(new head) to NULL
        setp(block + headerORFooter_SIZE, NULL);
    }
    else
    {
        // the free list is empty
        // set the next pointer of the block to NULL
        setp(block + headerORFooter_SIZE + prev_SIZE, NULL);
        // set the previous pointer of the block to NULL
        setp(block + headerORFooter_SIZE, NULL);
    }

    *free_list_head = block;
}

/************************************************
 *    HELPER FUNCTION: REMOVE_FROM_FREE_LIST    *
 * REMOVE THE BLOCK FROM THE DOUBLE LINKED LIST *
 *      AND UPDATE THE PREV AND NEXT PTRS       *
 ************************************************/
static void remove_from_free_list(void* block) {
    size_t size = extract_size(block);
    int index = get_free_list_index(size);

    void** free_list_head = &free_lists[index];

    void* prev = *(void**)(block + headerORFooter_SIZE);
    void* next = *(void**)(block + headerORFooter_SIZE + prev_SIZE);
    if (prev != NULL) {
        // set the next pointer of the previous block to the next block
        setp(prev + headerORFooter_SIZE + prev_SIZE, next);
    } else {
        // the block is the first block in the free list
        *free_list_head = next;
    }
    if (next != NULL) {
        // set the previous pointer of the next block to the previous block
        setp(next + headerORFooter_SIZE, prev);
    }
    setp(block + headerORFooter_SIZE, NULL);
    setp(block + headerORFooter_SIZE + prev_SIZE, NULL);
}

/*************************************************************************
 *                       HELPER FUNCTION: COALESCE                       *
 * COALESCE THE BLOCK WITH THE PREVIOUS AND NEXT BLOCKS IF THEY ARE FREE *
 *                           AFTER COALESCING:                           *
 *                 1.ADD THE NEW BLOCK TO THE FREE LIST                  *
 *            2.UPDATE THE PREV_IS_FREE BIT OF THE NEXT BLOCK            *
 *************************************************************************/
static void coalesce(void* block) {
    size_t size = extract_size(block);

    // Check if the previous block is free
    bool prev_is_free = extract_prev_is_free(block);
    if (prev_is_free == 0) {
        // Here the sequence matters!!!!!
        // 1. the previous block is free
        // 2. add the size of the previous block to the current block
        // 3. move the block_head pointer to the previous block
        // 4. remove the previous block from the free list
        size_t prev_size = extract_size(block - headerORFooter_SIZE);
        size += prev_size + headerORFooter_SIZE;
        block -= prev_size + headerORFooter_SIZE;
        remove_from_free_list(block);
    }

    // Check if the next block is free
    bool next_is_free = 1; // Initialize as false
    if (block + size + headerORFooter_SIZE < mem_heap_hi() - 7) { // Check if next block is within heap
        next_is_free = extract_curr_is_free(block + size + headerORFooter_SIZE);
    }
    if (next_is_free == 0) {
        //Here the sequence does not matter
        //add the size of the next block to the current block's size
        //no need to move the head pointer because the current block is the head
        //remove the next block from the free list
        remove_from_free_list(block + size + headerORFooter_SIZE);
        size_t next_size = extract_size(block + size + headerORFooter_SIZE);
        size += next_size + headerORFooter_SIZE;
    }
    

    // Update the Coalesced block's header and footer and add it to the free list
    set(block, (size << 1) << 1 | extract_prev_is_free(block));
    set(block + size , (size << 1) << 1 | extract_prev_is_free(block));
    add_to_free_list(block);

    // Update the extract_prev_is_free field of the next block
     if (block + size + headerORFooter_SIZE < mem_heap_hi() - 7) { // Check if next block is within heap
        void* next_block = block + size + headerORFooter_SIZE;
        if (extract_curr_is_free(next_block) == 0) {
            set(next_block, ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 0);
            set(next_block + extract_size(next_block), ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 0);
        }
        else{
            set(next_block, ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 0);
        }
     }
}

                                            /***************************
                                             * END OF HELPER FUNCTIONS *
                                             ***************************/

                                            /***************************
                                             * Start OF CORE FUNCTIONS *
                                             ***************************/
/********************************************************************************************
 *                                 LIST OF CORE FUNCTIONS:                                  *
 *                    1. MM_INIT: INITIALIZE THE HEAP AND THE FREE LISTS                    *
 *                          2. MALLOC: ALLOCATE A BLOCK OF MEMORY                           *
 *                             3. FREE: FREE A BLOCK OF MEMORY                              *
 *                         4. REALLOC: REALLOCATE A BLOCK OF MEMORY                         *
 * 5. CALLOC: ALLOCATE A BLOCK OF MEMORY AND SET IT TO ZERO (NOT USED IN MY IMPLEMENTATION) *
 ********************************************************************************************/

/*
 * mm_init: returns false on error, true on success.
 */
bool mm_init(void)
{
    // IMPLEMENT THIS
    void* heap = mem_sbrk(INIT_SIZE);
    if (heap == (void *)-1) {
        return false;
    }
    mm_memset(heap, 0, heap_size);
    heap_size = mm_heapsize();
    
    
    set (heap, 0);
    set (heap + headerORFooter_SIZE, 0x11);        // set prologue header
    set (heap + 2 * headerORFooter_SIZE, 0x11);    // set prologue footer
    set (heap + 3 * headerORFooter_SIZE, 0x1);       //set epilogue header
    // Initialize all free lists to NULL
    for (int i = 0; i < NUM_FREE_LISTS; i++) {
        free_lists[i] = NULL;
    }
    return true;
}

/*
 * malloc : returns a pointer to the allocated memory
 */
void* malloc(size_t size)
    {
        mm_checkheap(__LINE__);
        // Align the requested size: the minimum for a free block is 24 bytes,
        // so I set the minimum size for an allocated block to be 32 bytes(plus the size of metadata).
        // This way, the payload will be 16-byte aligned.

        size = alignx(size);

        // Traverse the free lists to find a suitable free block
        // First fit strategy is used
        for (int i = get_free_list_index(size); i < NUM_FREE_LISTS; i++) {
            int* fb = free_lists[i];

            while (fb != NULL) {
                size_t free_size = extract_size(fb);
                if (free_size >= size) {
                // found the first free block that is large enough
                
                // check if the block can be split -> the remaining size can be marked as free block
                if (free_size >= size + 4 * headerORFooter_SIZE) {
                    // split the block
                    //                   next_block = (char*)fb + free_size + headerORFooter_SIZE                
                        //                                                                       │                   
                        //                                                                       │                   
                        //                                           free_size                   │                   
                        //                        ◄─────────────────────────────────────────────►│                   
                        //                         alignx(size)                                  │                  ┼
                        //                        ◄─────────►                                    │                   
                        //                                  │                                    ▼                   
                        //                   ┌────┬─────────┼────┬────┬────┬────────────────┬────┬────┐              
                        // Malloc(size)      │    │         │Head│Prev│Next│    Payload     │Foot│    │              
                        //                   └────┴─────────┼────┴────┴────┴────────────────┴────┴────┘              
                        //                   ▲              │            remaining size                              
                        //                   │                 ◄─────────────────────────────────►                   
                        //                free_block                                                                 
                        //                  (fb)                                                                     

                    remove_from_free_list(fb);

                    // set the header for the allocated part
                    set(fb, (size << 1 | 0x01) <<1 | extract_prev_is_free(fb));    

                    // set the header and footer for the remaining part
                    size_t remaining_size = free_size - size - headerORFooter_SIZE;
                    set((char*)fb + size + headerORFooter_SIZE, (remaining_size << 1) << 1 | 0x01);
                    set((char*)fb + free_size, (remaining_size << 1) << 1 | 0x01);
                    add_to_free_list((char*)fb + size + headerORFooter_SIZE);

                    // update the next block after the old free block's prev_is_free bit
                    if ((char*)fb + free_size + headerORFooter_SIZE < (char*)mem_heap_hi() - 7) {
                        //the next block is not the epilogue block 
                        void* next_block = (char*)fb + free_size + headerORFooter_SIZE;
                        if (extract_curr_is_free(next_block) == 0) {
                            set(next_block, ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 0);
                            set(next_block + extract_size(next_block), ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 0);
                        }
                        else{
                            set(next_block, ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 0);
                        }
                    }
                    
                }
                else{
                    // the block cannot be split
                    // allocate the whole block
                    remove_from_free_list((char*)fb);

                    // set the header
                    set((char*)fb, (free_size << 1 | 0x01)<<1 | extract_prev_is_free(fb));

                    // set the next block's prev_is_free bit(everything else stays the same)
                    if ((char*)fb + free_size + headerORFooter_SIZE < (char*)mem_heap_hi() - 7) {
                        //the next block is not the epilogue block
                        void* next_block = (char*)fb + free_size + headerORFooter_SIZE;
                        if (extract_curr_is_free(next_block) == 0) {
                            set(next_block, ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 1);
                            set(next_block + extract_size(next_block), ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 1);
                        }
                        else{
                            set(next_block, ((extract_size(next_block) << 1) | extract_curr_is_free(next_block)) << 1 | 1);
                        }
                    }
                }

                return (char*) fb + headerORFooter_SIZE ;    // return the payload
                }
                fb = extract_next_free_blk(fb);
            }
        }
        // no suitable free block found, expand the heap
        size_t block_size = size + headerORFooter_SIZE;
        //check if the last block in the heap is free
        //Go to the last block's footer. If the last block is allocated, extracting its size will not make any sense.
        void*last_block = mm_heap_hi() - 7 - 8;
        size_t last_block_size = extract_size(last_block);
        void* last_block_start = last_block - last_block_size; //This value is not valid if the last block is allocated!!!!!!!!!!!
        bool is_in_free_list = 1;
        //check if the footer and header are the same and last_block_start in the free list
        //also need to check if last_block_start is a valid block in the heap to avoid segmentation fault.
        if(last_block_start >= mm_heap_lo() + 3 * headerORFooter_SIZE && last_block_start < mm_heap_hi() - 7 && last_block_start + last_block_size == last_block){
            if (extract_curr_is_free(last_block) == 0 && extract_size(last_block) == extract_size(last_block_start) && extract_prev_is_free(last_block_start) == 0){
            // check if the last block is in the free list
            size_t size = extract_size(last_block_start);
            int list_index = get_free_list_index(size);
            for (int i = list_index; i < NUM_FREE_LISTS; i++) {
                int* fb = free_lists[i];
                while (fb != NULL) {
                    if (fb == last_block_start) {
                        is_in_free_list = 0;
                        break;
                    }
                    fb = extract_next_free_blk(fb);
                }
                // break the outer loop if the block is found in the free list to increase efficiency
                if (is_in_free_list) {
                    break;
                }
            }
        }
        }
        // allocate the new block
        void* new_block = mem_sbrk(block_size) - headerORFooter_SIZE;
        if (new_block == (void *)-1) {
            return NULL; // error in expanding heap
        }

        // check if the heap is empty currently;
        if (mm_heapsize() == 32){
            // if the heap is empty, set the prev_is_free bit of the new block to 1 because the prologue is not free
            set(new_block, ((size << 1 | 0x01)<<1 )| 0x1);
            // update the epilogue header
            set(new_block + block_size, 0x1);
        }else{
            // if the heap is not empty, set the prev_is_free bit of the new block to current last block's free bit
            set(new_block, ((size << 1 | 0x01)<<1 )| (is_in_free_list));
            set(new_block + block_size, 0x1);

        }
        heap_size = mm_heapsize();
        void* ret = new_block + headerORFooter_SIZE;
        return ret;
    }
/*
 * free
 */
void free(void* ptr)
{
    mm_checkheap(__LINE__);
    // IMPLEMENT THIS
    void* block = ptr - headerORFooter_SIZE;
    // mark the block's metadata as free
    set(block, ((extract_size(block) << 1 ) << 1) | extract_prev_is_free(block) );
    set(block + extract_size(block), ((extract_size(block) << 1 ) << 1) | extract_prev_is_free(block) );
    coalesce(block);

    return;
}

/*
 * realloc
 */
void* realloc(void* oldptr, size_t size)
{
    mm_checkheap(__LINE__);
    // IMPLEMENT THIS
    if (oldptr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(oldptr);
        return NULL;
    }
    void* old_block = oldptr - headerORFooter_SIZE;
    size_t old_size = extract_size(old_block);
    if (old_size > alignx(size)) {
        // shrink the block
        if (old_size >= alignx(size) + 32 + 24) {
            // the old block can be split
            // set the header for the allocated part
            set(old_block, (alignx(size) << 1 | 0x01) << 1 | extract_prev_is_free(old_block));

            // set the header and footer for the remaining part (free)
            size_t remaining_size = old_size - alignx(size) - headerORFooter_SIZE;
            set(old_block + alignx(size) + headerORFooter_SIZE, (remaining_size << 1) << 1 | 0x01);
            set(old_block + old_size, (remaining_size << 1) << 1 | 0x01);
            free(old_block + alignx(size) + 2 * headerORFooter_SIZE);

            // update prev_is_free bit of the next block
            if (old_block + alignx(size) + 2 * headerORFooter_SIZE + extract_size(old_block + alignx(size) + headerORFooter_SIZE) < mem_heap_hi() - 7) {
                void* next_block1 = old_block + alignx(size) + 2 * headerORFooter_SIZE + extract_size(old_block + alignx(size) + headerORFooter_SIZE);
                if (extract_curr_is_free(next_block1) == 0) {
                    set(next_block1, ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 | 0);
                    set(next_block1 + extract_size(next_block1), ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 | 0);
                }
                else{
                    set(next_block1, ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 | 0);
                }
            }
            return oldptr;
        }
        else {
            // the old block cannot be split
            return oldptr;

        }
    }
    else if (old_size == alignx(size)) {
        return oldptr;
    }
    else {
        // the next block is free and the remaining part can be marked as free block
        //                                    OLD SIZE                                                                   
        //       ◄──────────────────────────────────────────────────────────────────────────────────►                    
        //         alignx(size)               needing_free_size                     Remaining Size                       
        //       ◄─────────────► ◄───────────────────────────────────────────► ◄────────────────────►                    
        //                                                                    │                                          
        // ┌────┬───────────────┬────┬────┬────┬──────────────────────────────┼────────────────┬────┬────┬──────────────┐
        // │    │               │Head│Prev│Next│                    Payload   │                │Foot│    │NEXT BLK      │
        // └────┴───────────────┴────┴────┴────┴──────────────────────────────┼────────────────┴────┴────┴──────────────┘
        //                                                                    │        ▲            ▲                    
        //  old_block + alignx(size) + headerORFooter_SIZE                             │            │                    
        //                           ┌────┬────┬────┬─────────┬────┐                   │            │                    
        //                     │     │Head│Prev│Next│Payload  │Foot├───────────────────┘            │                    
        //                     └───► └────┴────┴────┴─────────┴────┘                                │                    
        //                                                    ▲                                     │                    
        //                                                    │                                     │                    
        //   old_block + alignx(size) + remaining_size────────┘                                     │                    
        //                                                                                          │                    
        //   old_block + alignx(size) + extract_size(old_block + alignx(size)) + 2* headerORFooter_SIZE                  
        size_t needing_free_size = alignx(size) - old_size;
        void* next_block = old_block + old_size + headerORFooter_SIZE;
        if (extract_curr_is_free(next_block) == 0 && extract_size(next_block) + 8 >= needing_free_size){
            size_t remaining_size = extract_size(next_block) - needing_free_size + headerORFooter_SIZE;
            if(remaining_size >= 4* headerORFooter_SIZE + 24){
                

                remove_from_free_list(next_block);

                set(old_block, ((alignx(size) << 1) | 0x01) << 1 | extract_prev_is_free(old_block));

                // set the header and footer for the remaining part (free)
                set((char*)old_block + alignx(size) + headerORFooter_SIZE, ((remaining_size - 8) << 1) << 1 | 0x01);
                set((char*)old_block + alignx(size) + remaining_size, ((remaining_size - 8) << 1) << 1 | 0x01);
                free(old_block + alignx(size) + 2 * headerORFooter_SIZE);

                // update prev_is_free bit of the next block
                if (old_block + alignx(size) + extract_size(old_block + alignx(size)) + 2* headerORFooter_SIZE < mem_heap_hi() - 7) {
                    void* next_block1 = old_block + alignx(size) + extract_size(old_block + alignx(size)) + 2* headerORFooter_SIZE;
                    if (extract_curr_is_free(next_block1) == 0) {
                        set(next_block1, ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 |0);
                        set(next_block1 + extract_size(next_block1), ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 | 0);
                    }
                    else{
                        set(next_block1, ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 | 0);
                    }
                }
                
                
                return oldptr;
                }
            else{
                // the next block is free but the remaining part cannot be marked as free block
                // mark the whole freee block as allocated
                //        NEW SIZE  extract_size(old_block) + extract_size(next_block) + headerORFooter_SIZE                     
                //       ◄──────────────────────────────────────────────────────────────────────────────────►                    
                //                    alignx(size)                                                                               
                //       ◄─────────────────────────────────────►                                                                 
                //                                                                                                               
                // ┌────┬───────────────┬────┬────┬────┬───────────────────────────────────────────────┬────┬────┬──────────────┐
                // │    │               │Head│Prev│Next│                    Payload                    │Foot│    │NEXT BLK      │
                // └────┴───────────────┴────┴────┴────┴───────────────────────────────────────────────┴────┴────┴──────────────┘
                //                                                                                          ▲                    
                //                                                                                          │                    
                //                                                                                          │                    
                //                                                                                                               
                //                                                      old_block + extract_size(old_block) + headerORFooter_SIZE

                // set the header
                
                remove_from_free_list(next_block);
                
                set(old_block, ((extract_size(old_block) + extract_size(next_block) + headerORFooter_SIZE) << 1 | 0x01) << 1 | extract_prev_is_free(old_block));    

                // update prev_is_free bit of the next block
                if (old_block + extract_size(old_block) + headerORFooter_SIZE < mem_heap_hi() - 7) {
                    void* next_block1 = old_block + extract_size(old_block) + headerORFooter_SIZE;
                    if (extract_curr_is_free(next_block1) == 0) {
                        set(next_block1, ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 | 1);
                        set(next_block1 + extract_size(next_block1), ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 | 1);
                    }
                    else{
                        set(next_block1, ((extract_size(next_block1) << 1) | extract_curr_is_free(next_block1)) << 1 | 1);
                    }
                }
                
                return oldptr;
                }
                
        } else {
            // the next block is free but the remaining part cannot be marked as free block
            // reallocate the whole free block
            void* newptr = malloc(size);
            if (newptr == NULL) {
                return NULL;  // malloc failed
            }
            mm_memcpy(newptr, oldptr, old_size);  // copy the old data
            memset((char*)newptr + old_size, 0, alignx(size) - old_size);  // set the other bytes to 0
            mm_free(oldptr);
            return newptr;
        }
    }
}

/*
 * calloc
 * ThisHelper function is not tested by mdriver, and has been implemented for you.
 */
void* calloc(size_t nmemb, size_t size)
{
    void* ptr;
    size *= nmemb;
    ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Returns whether the pointer is in the heap.
 * May be useful for debugging.
 */
static bool in_heap(const void* p)
{
    return p <= mm_heap_hi() && p >= mm_heap_lo();
}

/*
 * Returns whether the pointer is aligned.
 * May be useful for debugging.
 */
static bool aligned(const void* p)
{
    size_t ip = (size_t) p;
    return align(ip) == ip;
}

/*
 * mm_checkheap
 * You call theHelper function via mm_checkheap(__LINE__)
 * The line number can be used to print the line number of the calling
 *Helper function where there was an invalid heap.
 */
bool mm_checkheap(int line_number)
{
#ifdef DEBUG
    // Write code to check heap invariants here
    // IMPLEMENT THIS
   // CHeck freelist
    for (int i = 0; i < NUM_FREE_LISTS; i++) {
        void* fb = free_lists[i];
        while (fb != NULL) {
            if (!in_heap(fb)) {
                dbg_printf("Error: block %p is not in heap at line %d\n", fb, line_number);
                return false;
            }
            if (!aligned(fb)) {
                dbg_printf("Error: block %p is not aligned at line %d\n", fb, line_number);
                return false;
            }
            if (extract_curr_is_free(fb) != 0) {
                dbg_printf("Error: block %p is not marked as free at line %d\n", fb, line_number);
                return false;
            }
            
            if (extract_size(fb) < 24) {
                dbg_printf("Error: block %p has size less than 24 at line %d\n", fb, line_number);
                return false;
            }
            fb = extract_next_free_blk(fb);
        }
    }
    // Check the epilogue block
    char* epilogue = mm_heap_hi() - 7;
    if (*epilogue != 0x01) {
        dbg_printf("Error: epilogue block has been overwritten at line %d\n", line_number);
        return false;
    }
    // Check the prologue block
    char* prologueh = mm_heap_lo() + headerORFooter_SIZE;
    char* prologuef = mm_heap_lo() + 2 * headerORFooter_SIZE;
    if (*prologueh != 0x11 || *prologuef != 0x11) {
        dbg_printf("Error: prologue block has been overwritten at line %d\n", line_number);
        return false;
    }
   
#endif // DEBUG
    return true;
}
