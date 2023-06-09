#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include "bitmap.h"
#include "block_store.h"
// include more if you need

// You might find this handy.  I put it around unused parameters, but you should
// remove it before you submit. Just allows things to compile initially.
#define UNUSED(x) (void)(x)

// Implementation of the block store struct. 
typedef struct block_store 
{
    bitmap_t *fbm; 
    void *blocks[BLOCK_STORE_NUM_BLOCKS]; 
} block_store_t;


block_store_t *block_store_create()
{
    // Create the block store object. 
    block_store_t *bs = malloc(sizeof(block_store_t));
    if (!bs)
    {
        return NULL;
    } 

    // Initialize the FBM. 
    bitmap_t *fbm = bitmap_create(BLOCK_STORE_NUM_BLOCKS);
    if (!fbm)
    {
        return NULL; 
    }
    bs -> fbm = fbm;

    return bs;
}

void block_store_destroy(block_store_t *const bs)
{
   // If it exists, destroy the block store and its FBM. 
   if (bs) 
   {
        bitmap_destroy(bs -> fbm);
        free(bs);
   }
}


size_t block_store_allocate(block_store_t *const bs)
{
    // Bad inputs. 
    if (!bs)
    {
        return SIZE_MAX;
    } 

    // Find the first free block and allocate it. 
    size_t id = bitmap_ffz(bs -> fbm);
    if (id != SIZE_MAX && id != BLOCK_STORE_AVAIL_BLOCKS)
    {
        // Set the corresponding bit in the FBM. 
        bitmap_set(bs->fbm, id);
		(bs -> blocks)[id] = calloc(BLOCK_SIZE_BYTES, 1); 
        return id; 
    }
    return SIZE_MAX;
}


bool block_store_request(block_store_t *const bs, const size_t block_id)
{
    // Check for bad inputs. 
    if (bs != NULL) 
    {
		if (bs -> fbm != NULL) 
        {
			if (block_id < BLOCK_STORE_NUM_BLOCKS) 
            {
                // Return if requested block in use. 
                if (bitmap_test(bs -> fbm, block_id)) 
                {
                    return false;
                }
                
                // Set the corresponding bit in the FBM. 
                bitmap_set(bs -> fbm, block_id);
				(bs -> blocks)[block_id] = calloc(BLOCK_SIZE_BYTES, 1); 
                return true;
			}
		}
    }
    return false;
}

// Frees the specified block
void block_store_release(block_store_t *const bs, const size_t block_id)
{
    // Check for bad inputs. 
    if (bs != NULL) 
    {
		if (bs -> fbm != NULL) 
        {
			if (block_id < BLOCK_STORE_NUM_BLOCKS) {
				// Release the . and clear . 
                free((bs -> blocks)[block_id]);
				(bs -> blocks)[block_id] = NULL;
				bitmap_reset(bs -> fbm, block_id);
			}
		}
    }
}


size_t block_store_get_used_blocks(const block_store_t *const bs)
{
    // Check for bad inputs. 
    if (bs == NULL || bs -> fbm == NULL) 
    {
        return SIZE_MAX;
    }
	
    // Return # of available blocks. 
    return bitmap_total_set(bs -> fbm);
}

size_t block_store_get_free_blocks(const block_store_t *const bs)
{
    // Check for bad inputs. 
    if (bs == NULL || bs -> fbm == NULL) 
    {
        return SIZE_MAX;
    }
	
	// Return # of free blocks. 
    return BLOCK_STORE_AVAIL_BLOCKS - bitmap_total_set(bs -> fbm);
}


size_t block_store_get_total_blocks()
{
    return BLOCK_STORE_AVAIL_BLOCKS;
}

size_t block_store_read(const block_store_t *const bs, const size_t block_id, void *buffer)
{
    // Check for bad inputs. 
    if (bs == NULL || buffer == NULL || block_id >= BLOCK_STORE_NUM_BYTES || block_id == 0) 
    {
        return 0;
    }

    // Copy the block's contents into the given buffer.  
    if ((bs -> blocks)[block_id])
    {
		memcpy(buffer, ((bs -> blocks)[block_id]), BLOCK_SIZE_BYTES);
	}
    return BLOCK_SIZE_BYTES;
}


size_t block_store_write(block_store_t *const bs, const size_t block_id, const void *buffer)
{
    // Check for bad inputs.
    if (bs == NULL || buffer == NULL || block_id > BLOCK_STORE_NUM_BYTES) 
    {
        return 0;
    }

    // Copy the buffer's contents into the block.
	if ((bs -> blocks)[block_id])
    {
		memcpy(((bs -> blocks)[block_id]), buffer, BLOCK_SIZE_BYTES);
	}
    return BLOCK_SIZE_BYTES;
}


block_store_t *block_store_deserialize(const char *const filename)
{
    // Check for bad inputs. 
    if (filename == NULL) 
    {
        return NULL;
    }
	
    // Create block store. 
    block_store_t * bs = block_store_create();
	if (!bs)
	{
		return NULL;
	}
	
	char *buf = malloc(BLOCK_SIZE_BYTES);

	// First need to get fbm to see which blocks to read and which to skip.
	int fd = open(filename, O_RDONLY);
	if (fd == -1)
	{
		printf("Deserialize open file error: %s/n", strerror(errno));
	}
	
    // Go to the 127th block, i.e. where the FBM should be.
	if (lseek(fd, 127 * BLOCK_SIZE_BYTES, SEEK_CUR) == -1)
	{
		printf("Deserialize Error (lseek): %s/n", strerror(errno));
	}
	
    // Read the FBM into the buffer.
	if (read(fd, buf, BLOCK_SIZE_BYTES) == -1)
	{
		printf("Deserialize Error (read): %s/n", strerror(errno));
	}
	// Save FBM. 
	bitmap_t *bitmap = bitmap_import(BLOCK_STORE_NUM_BLOCKS, buf);
	
	// Reset file offset. 
	if (lseek(fd, 0, SEEK_SET) == -1)
	{
		printf("Deserialize Error (lseek): %s/n", strerror(errno));
	}

	// Loop through FBM, allocating blocks that are marked as in use. 
    for (int i = 0; i < 256; i++)
	{
		if (read(fd, buf, BLOCK_SIZE_BYTES) == -1)
		{
			printf("Deserialize Error (read): %s/n", strerror(errno));
		}
		
		if (bitmap_test(bitmap, i))
		{
			// Update new FBM. 
			if (!block_store_request(bs, i)){
                return NULL;
            } 
			// Populate the block. 
			if (block_store_write(bs, i, buf) != BLOCK_SIZE_BYTES) 
            {
                return NULL;
            }
		}
	}

    free(buf);
    free(bitmap);
    return bs; 
}

size_t block_store_serialize(const block_store_t *const bs, const char *const filename)
{
    // Check for bad inputs. 
    if (!bs || !filename || !strcmp(filename, "\n") || !strcmp(filename, "\0") || !strcmp(filename, ""))
    {
        return 0;
    } 

    // System call to create or open the file. 
    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0777);
    if (fd == -1)
    {
		printf("Serialize Error (open): %s/n", strerror(errno));
	}

    char *buf = malloc(BLOCK_SIZE_BYTES);

    // Serialize the data. 
    size_t i = 0; 
    for (i = 0; i < 256; i++) 
    {
        // Special case for FBM. 
		if (i == 127)
		{
			memcpy(buf, bitmap_export(bs->fbm), BITMAP_SIZE_BYTES); 
		}
        else if (!bitmap_test(bs->fbm, i))
        {
            memset(buf, '0', BLOCK_SIZE_BYTES);
        }
        else 
        {
            memcpy(buf, (bs->blocks)[i], BLOCK_SIZE_BYTES);
        }
    
        // Write the data to the file. 
        if (write(fd, buf, BLOCK_SIZE_BYTES) == -1) 
        {
			printf("Serialize Error (write): %s/n", strerror(errno));
		}
    }
	
    // Close the file. 
	if (close(fd) == -1) 
	{
		printf("Serialize Error (close): %s/n", strerror(errno));
	}
	
    free(buf);
    return BLOCK_STORE_NUM_BYTES;
}