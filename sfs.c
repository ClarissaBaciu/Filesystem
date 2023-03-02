
/* sfs.c 
 * 
 * Author : clarissabaciu (260929976)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sfs_api.h"
#include "disk_emu.h"
#include "disk_emu.c"
#include <math.h> //run with lm flag


//CONSTANTS
#define BLOCK_SIZE              1024     //block size in bytes
#define AVG_FILE_SIZE           10       //average file size in blocks
#define MAX_FILE_NUM            100      //max number of files /SET TO 150 LATER?
#define DIR_SIZE                10      //1024/64 byte entries = 16 entries/block, 150/16 = 10
#define INODE_TBL_SIZE          10      //16 fields * 4 bytes each = 64 bytes/inode, 150/(1024/64)=10
#define FBM_SIZE                2       //Free bitmap, 1511 blocks total, 1511/1024 = 2 

#define TOTAL_INODE_ENTRIES     160      //should be 64 byte entries, 16 entries/block, 16*inode_block_number entries, 16*10 = 160

#define FILE_SYST_SIZE          2000     //32 for now(debugging purposes), can be set to 1513 or 2000 later?         

#define APPEND_MODE             1       //pointer at the end of the file
#define UNUSED_MODE             0       //file is not present in ofdt
#define SEEK_MODE               2       //pointer has been seeked

#define MAXFILENAME             32      

#define LOG                     0       //to print values



//STRUCTURES

//block type structure definition
typedef struct _block_t{
    char data[BLOCK_SIZE];
}block_t;

//super block type structure definition
typedef struct _superblock_t{ 
    //each entry is 4 bytes
    // char magic[4];              //char = 1 byte'
    int magic;
    int block_size;             //int = 4 bytes 
    int file_syst_size;         //#blks used for file syst
    int inodetbl_size;          //#blks used for inode table 
    int fbm_size;               //#blks used for free bit map size
    int inodetbl_loc;           //inode table location (block index)
    int fbm_loc;                //fbm locaiton (block index)
    int root_inode_num;         //inode #  used for rood directory 
}superblock_t;

//i-node entry type structure definition
typedef struct _inode_t{
    //16 fields with 4 bytes each => 64 bytes
    int mode; //append mode = 1
    int link_cnt;
    int size;      //in bytes 
    int pointers[12];//assume pointers to file blocks are indices (int) so they take up 4 bytes
    int ind_pointer;
}inode_t;

//i-node table type structure definition 
typedef struct _inode_table_t{
    inode_t inode; //should be 64 byte entries, 16 entries/block, 16*inode_block_number entries
}inode_table_t;

//directory entry type structure definition
typedef struct _dir_entry_t{
    char filename[64 - sizeof(int)];
    int inode; 
}dir_entry_t;

//structure to hold indirect pointers
typedef struct _indirect_ptrs_t{ //1024/4 = 256 pointers per block
    int ptr;
}indirect_ptrs_t;

typedef struct _fbm_map_t{
    char available;  //1 for available , 0 for unavailable
}fbm_map_t;

typedef struct _ofdt_t{ //8byte entries
    int inode;  //inode index of this file
    int offset; //read and write pointer (in bytes?)
}ofdt_t;

typedef struct _data_t{
    char character;  //1 byte entries
}data_t;



//GLOBAL VARIABLES (blocks in memory)
block_t superblock_mem[1];                  // super block (composed of 1 block)
block_t inode_tbl_mem[INODE_TBL_SIZE];      //inode table 
block_t dir_mem[DIR_SIZE*11];               // root directory (limit to 10 blocks/directory)
block_t fbm_map_mem[FBM_SIZE];              //free bit map
block_t indirect_ptrs_mem[1];               //1 block of indirect pointers
block_t data_blk_mem[BLOCK_SIZE];           //datablock 
ofdt_t ofdt[MAX_FILE_NUM];                  //open file descriptor table


//variables
int fbm_loc;            //fbm location on disk (in blks)
int inodetbl_loc;       //inode table location on disk (in blks)
int data_loc;           //data blocks location on disk
int directory_inode;    //inode number attributed to directory (should be 0)
int current_file;       //pointer used to iterate through files in directory

//helper functions

// function looks at fbm and assigns a new block based on availability
//returns disk_blk_num
int find_free_block(){
    int disk_blk_num=0;
    int fbm_index = 0; //fbm index of new block assigned
    fbm_map_t *fbm_map = (fbm_map_t *)fbm_map_mem;
    for (int fb = 0; fb < FILE_SYST_SIZE; fb++){
        if (fbm_map[fb].available){ //if there is an availble block, assign this one as new disk memory block
            fbm_index = fb;
            fbm_map[fb].available = 0; //mark as unavailable
            break;
        }
    }
    if (fbm_index == 0){ //if the index is still 0 there are no more available blocks
        return -1;
    }
    write_blocks(fbm_loc, FBM_SIZE, fbm_map_mem);     //write fbm into memory
    //convert from fbm index to data block index
    disk_blk_num = fbm_index-data_loc;  //convert from fbm index to data block index(start at 0 with first data block) 
    return disk_blk_num; //return disk block number of new datablock
}


//write directory to memory selecting appropriate data block according to directory entry
void write_dir_to_memory(int dir_entry_num){
    dir_entry_t *dir = (dir_entry_t *)dir_mem;
    int mem_blk_num = (int)(floor((dir_entry_num) / (double) (BLOCK_SIZE/64))); //floor division pointer/block size => block number
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; 
    inode_t *dir_inode = (inode_t *)&inode_table[0];      
    int disk_blk_num = dir_inode->pointers[mem_blk_num];
    if (disk_blk_num == 0){ //if it does not exist yet, assign it 
        disk_blk_num = find_free_block(); //find a free block 
        dir_inode->pointers[mem_blk_num] = disk_blk_num;   //write to inode  
        write_blocks(inodetbl_loc,1,inode_tbl_mem);//write to memory
    }
    write_blocks(data_loc + disk_blk_num,1,dir+(mem_blk_num*16)); //write into disk each +1 is +64 bytes in dir_mem
}

//fct to check validity of fd
int fd_valid(int fd){
    //check if entry is larger than the max number of entries or is negative
    if (fd>MAX_FILE_NUM || fd<0){ 
        return -1; 
    }
    //check if entry is in use
    int inode_num = ofdt[fd].inode;
    if (inode_num == 0){
        return -1; 
    }
    return 1;
}

//returns minimum of 2 integers
int min(int x, int y){
    if (x<y){
        return x;
    }else if (y<x){
        return y;
    }

    return x; 
}

//prints data blk
void print_data_blk(){
    data_t *data_blk = (data_t *)data_blk_mem;
    for (int byte = 0; byte<BLOCK_SIZE; byte++){
        printf("--- DATA BLOCK --- \n");
        printf("%c",data_blk[byte].character);
        printf("\n");
    }
}

//prints ofdt, filename and inode
void print_ofdt(){
    //ofdt
    printf(" in memory OFDT:  \n");
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
              //second cast to inode table entry
    for (int of = 0; of < MAX_FILE_NUM; of++){   //initialize with 0s 
        if (ofdt[of].inode!=0){
            dir_entry_t *dir = (dir_entry_t *)dir_mem; 
            for (int i = 0; i < MAX_FILE_NUM; i++){ 
                if (dir[i].inode == ofdt[of].inode){
                    printf("filename: %s",dir[i].filename);
                }
            }
            
            printf("| inode : %d",ofdt[of].inode);
            printf(" | offset : %d",ofdt[of].offset);
            inode_t *cur_inode = (inode_t *)&inode_table[ofdt[of].inode];   
            printf(" | filesize : %d \n",cur_inode->size);


        }
        
    }
    


}

//print inode
void print_inode(){
     //inode_tbl_mem
    printf("\nin memory inode table: \n");
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
    inode_t *dir_inode = (inode_t *)&inode_table[0];             //second cast to inode table entry


    for (int i = 0; i < TOTAL_INODE_ENTRIES; i++){ 
        inode_t *inode = (inode_t*)&inode_table[i];
        if (inode->link_cnt){ //if used print values
            printf("----\nINODE: %d", i  );
            printf(" | mode: %d",inode->mode );
            printf(" | size: %d\n",inode->size );
            printf("\n Pointers: ");
            for (int y = 0; y < 12; y++){ 
                printf("%d, ",inode->pointers[y]);
            }
            printf("\n");
        }
    }
}

//print multiple values
void print_values(){
    printf("----- BEGIN PRINT -----\n");

    // printf("fbm_loc index : %d \n",fbm_loc);
    // printf("inodetbl_loc index : %d \n",inodetbl_loc);
    // printf("directory inode index : %d \n",directory_inode);

    //superblock
    // printf("\nin memory superblock: \n");
    // superblock_t *sb = (superblock_t *)superblock_mem;  
    // printf("magic # : %d\n",sb->magic );
    // printf("block_size : %d\n",sb->block_size );
    // printf("file_syst_size : %d\n",sb->file_syst_size );
    // printf("inodetbl_size : %d\n",sb->inodetbl_size );
    // printf("fbm_size : %d\n",sb->fbm_size );
    // printf("inodetbl_loc : %d\n",sb->inodetbl_loc );
    // printf("fbm_loc : %d\n",sb->fbm_loc );
    // printf("root_inode_num : %d\n",sb->root_inode_num );

    //inode_tbl_mem
    printf("\n----INODE TABLE--- \n");
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
    inode_t *dir_inode = (inode_t *)&inode_table[0];             //second cast to inode table entry


    for (int i = 0; i < TOTAL_INODE_ENTRIES; i++){ 
        inode_t *inode = (inode_t*)&inode_table[i];
        if (inode->link_cnt){ //if used print values
            printf("----\nINODE: %d", i  );
            printf(" | mode: %d",inode->mode );
            printf(" | size: %d\n",inode->size );
            printf("\n Pointers: ");
            for (int y = 0; y < 12; y++){ 
                printf("%d, ",inode->pointers[y]);
            }
            printf("\n");
        }
    }
    
    //directory
    printf("\n---DIRECTORY---\n");
    dir_entry_t *dir = (dir_entry_t *)dir_mem; 
    for (int i = 0; i < MAX_FILE_NUM; i++){ 
        if (dir[i].inode){
            printf("filename: %s, ",dir[i].filename);
            printf("inode: %d\n",dir[i].inode);
        }
    }

    //fbm map
    printf("\n ---FBM MAP--- \n");
    fbm_map_t *fbm_map = (fbm_map_t *)fbm_map_mem;
    for (int i = 0; i < FILE_SYST_SIZE; i++){ 
        printf("%d",fbm_map[i].available);
    }

    //ofdt
    printf("\n----OFDT---- \n");
    for (int of = 0; of < MAX_FILE_NUM; of++){   //initialize with 0s 
        printf("inode : %d, ",ofdt[of].inode);
        printf("offset : %d\n",ofdt[of].offset);
    }

    printf("----- END PRINT -----\n");
}


//create file system on disk
void mksfs(int f){ 
    char filename[64 - sizeof(int)] = "sfs"; //declare & initialize filename

    if (f){  //flag is true(1), create new file system
   
        //initialize global variables
        fbm_loc = FILE_SYST_SIZE-FBM_SIZE;
        inodetbl_loc = 1; //after super block
        directory_inode = 0; //first inode should represent directory
        data_loc = inodetbl_loc + INODE_TBL_SIZE;
        current_file = 0;

        init_fresh_disk(filename, BLOCK_SIZE, FILE_SYST_SIZE);//provide array of disk blocks 

        // create new super block and write to disk
        superblock_t *sb = (superblock_t *)superblock_mem;   //cast to superblock
        (*sb).magic = 28980674; //magic number reference in document
        (*sb).block_size = BLOCK_SIZE;
        (*sb).file_syst_size = FILE_SYST_SIZE;
        (*sb).inodetbl_size = INODE_TBL_SIZE;
        (*sb).fbm_size = FBM_SIZE;

        (*sb).inodetbl_loc = inodetbl_loc; //loc = block index
        (*sb).fbm_loc = fbm_loc;
        (*sb).root_inode_num = directory_inode;   //should be first index in inode table -> contiguous   

        write_blocks(0, 1, superblock_mem); //write super block to memory (starting address = block index)

        //initialize i-node cache and write to disk  (with first inode set to directory ->first data blk)
        inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
        inode_t *dir_inode = (inode_t *)&inode_table[0];             //second cast to inode table entry
        dir_inode->link_cnt = 1; //to signify directory i-node is taken 
        dir_inode->pointers[0] = 0;//set initial pointer to 0th index of the data blocks (where the first directory block will be by default)

        for (int i = 1; i < TOTAL_INODE_ENTRIES; i++){ //set all other link_cnts to 0 to mark as unused
            inode_t *inode = (inode_t*)&inode_table[i];
            inode->link_cnt = 0;
        }

        write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);     //write into memory

        //initialize directory in memory
        dir_entry_t *dir = (dir_entry_t *)dir_mem; 
        for (int i = 0; i < MAX_FILE_NUM; i++){   //initialize with 0s 
            memset(dir[i].filename, '\0', sizeof(dir[i].filename));
            dir[i].inode = 0;
        }
        write_blocks(1+INODE_TBL_SIZE, 1, dir_mem);  //assume this is the first block in mem


        // free bitmap
        fbm_map_t *fbm_map = (fbm_map_t *)fbm_map_mem; //typecast
        int occupied_blks = 1 + INODE_TBL_SIZE + 1; //1 superblock + 10 blks for inode table + 1 blk for first directory data blk
        for (int i = 0; i < occupied_blks; i++){    //mark as occupied for occupied blocks
            (&fbm_map[i])->available = 0; 
        }
        for (int y = occupied_blks; y < fbm_loc; y++){  //fill up rest with 1s to mark as available
            (&fbm_map[y])->available = 1; 
        }
        for (int x = fbm_loc; x < FILE_SYST_SIZE; x++){  //last two blocks unavailable due to fbm
            (&fbm_map[x])->available = 0;  //this syntax also seems to work
        }
        write_blocks(fbm_loc, FBM_SIZE, fbm_map_mem);     //write into memory

        //open-file descriptor table (only in memory)
        for (int of = 0; of < MAX_FILE_NUM; of++){   //initialize with 0s 
            ofdt[of].inode = 0;  
            ofdt[of].offset = 0; 
        }

    }else{  //flag is false(0), valid file system already present(super block is valid)
        init_disk(filename, BLOCK_SIZE, FILE_SYST_SIZE);
        
        //retrieve disk data
        read_blocks(0, 1, superblock_mem);
        fbm_loc = ((superblock_t *)superblock_mem)->fbm_loc;
        inodetbl_loc = ((superblock_t *)superblock_mem)->inodetbl_loc;
        data_loc = inodetbl_loc + INODE_TBL_SIZE; //initialize location of data blocks after inode table
        current_file = 0;    //set current file to 0 for sfs_getnextfile

        //read in inode table and fbm
        read_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem); 
        read_blocks(fbm_loc, FBM_SIZE, fbm_map_mem);
        
        //open-file descriptor table (only in memory)
        for (int of = 0; of < MAX_FILE_NUM; of++){   //initialize with 0s 
            ofdt[of].inode = 0;  
            ofdt[of].offset = 0; 
        }
    }
}


int sfs_fopen(char* fn){
    if (strlen(fn) > MAXFILENAME){ //make sure filename is proper size
        return -1;
    }
    int fd; //file descriptor 
    int found = 0;
    int inode_num = 0;
    dir_entry_t *dir = (dir_entry_t *)dir_mem; 
    //search through directory for entry
    for (int i = 0; i < MAX_FILE_NUM; i++){  
        if (strcmp(dir[i].filename,fn) == 0){
            found = 1; //set found to 1
            inode_num = dir[i].inode;
        }
    }
    if (found){     //case 1, opening old file
        //get size from inode table
        inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; 
        inode_t *cur_inode = (inode_t *)&inode_table[inode_num];             
        if (!cur_inode->link_cnt){ //verify link_count is marked as 1
            printf("error link count for this file inode should be 1");
        }
        //get file size
        int filesize = cur_inode->size; // get filesize 
        cur_inode->mode = APPEND_MODE; //set mode to append, pointer-> at the end of the file

        //write inode table back into disk 
        write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);  

        int found = 0; //if already present in table
       
        //check if inode is already present 
        for (int i=0;i<MAX_FILE_NUM;i++ ){
            if (ofdt[i].inode == inode_num){
                ofdt[i].offset = filesize;
                found = 1; 
                fd = i;
                break;
            }
        }
        //if inode is not already present,create ofdt entry & set offset to filesize in ofdt
        if (!found){
            for (int i=0;i<MAX_FILE_NUM;i++ ){
                if (ofdt[i].inode == 0){ //find first open ofdt
                    fd = i; //ofdt entry
                    ofdt[i].inode = inode_num; //set inode number
                    ofdt[i].offset = filesize; //set filesize
                    break; //break out of loop once inode has been found
                }    
            }             
        }

        return fd;

    }else{ //case 2, new file
        //go to inode table find free entry, update link count, set size to 0
        inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; 
        for (int i = 0; i < TOTAL_INODE_ENTRIES; i++){ //find first inode entry that is not used
            inode_t *inode = (inode_t*)&inode_table[i];
            if(inode->link_cnt == 0){
                inode_num = i; //save inode index for directory
                inode->link_cnt = 1; //update link count
                inode->size = 0; //set size to 0
                break; //stop here
            }
        }   
        //write inode table to disk
        write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);   

        int dir_entry_num = 0;

        //find directory entry that is free. put filename. and inode number
        dir_entry_t *dir = (dir_entry_t *)dir_mem; 
        for (int i = 0; i < MAX_FILE_NUM; i++){  //find first empty entry
            if (dir[i].inode == 0){
                dir[i].inode = inode_num;
                strcpy(dir[i].filename,fn); //assign given filename
                dir_entry_num = i;
                break; 
            }
        }

        //find disk location of current dir_entry to rewrite into disk
        write_dir_to_memory(dir_entry_num);

        //update ofdt entry with inode and offset with the filesize (0)
        for (int i=0;i<MAX_FILE_NUM;i++ ){
            if (ofdt[i].inode == 0){ //find first open ofdt
                fd = i; //ofdt entry
                ofdt[i].inode = inode_num; //set inode number
                ofdt[i].offset = 0; //set filesize
                break; //break out of loop once inode has been found
            }    
        }
        //return index of this entry
        return fd;
    }

}

//assume close will alway be called before removed
//close a file in ofdt
int sfs_fclose(int fd){
    //check if fd entry is valid 
    if (LOG){printf("-> closing fd : %d \n", fd);}
    if (fd_valid(fd) < 0){
        return -1;
    }

    int inode_num = ofdt[fd].inode;

    ofdt[fd].inode = 0;    //reset
    ofdt[fd].offset = 0;   //reset
    //set to unused mode in inode
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; 
    inode_t *cur_inode = (inode_t *)&inode_table[inode_num];
    cur_inode->mode = UNUSED_MODE;   //set to unused mode to indicate it is not in the ofdt
    //write to memory
    write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);   
    return 0;
}

//move pointer to location
int sfs_fseek(int fd, int loc){
    if (LOG){printf("-> Seeking fd : %d, to loc : %d \n", fd, loc);}
    //loc in number of bytes from 0th index 
    //larger than file size 
    if (fd_valid(fd) < 0 || loc < 0){ //check fd validity
        return -1;
    }
    //retrieve file size to make sure pointer value is not larger
    int inode_num = ofdt[fd].inode;
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
    inode_t *cur_inode = (inode_t *)&inode_table[inode_num];   
    if(loc > (cur_inode->size)-1){ 
        return -1;
    }
    //change mode in inode
    if (loc == 0){
        cur_inode->mode = UNUSED_MODE;
    }else if (loc == (cur_inode->size)-1){
        cur_inode->mode = APPEND_MODE;
    }else{
        cur_inode->mode = SEEK_MODE;
    }
    //write inode back into memory 
    write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);   
    //modify pointer
    ofdt[fd].offset = loc;
    return 0;
}





int sfs_fwrite(int fd, const char *buf, int length){ 
    if (fd_valid(fd)<0){    //check fd validity
        printf("invalid fd\n");
        return -1;
    }
    int inode_num = ofdt[fd].inode; //retrieve inode number and pointer from ofdt
    int pointer = ofdt[fd].offset;

    if(LOG){printf("\n\n-> Writing %d bytes from inode_num : %d, which  has offset : %d \n",length,inode_num,pointer);}  

    read_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);   //read inode table 
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
    inode_t *cur_inode = (inode_t *)&inode_table[inode_num];   
    
    //initialize variables
    int disk_blk_num = 0; //block number on disk 
    int buf_offset = 0;   //offset within buffer (data written overall)
    int mem_blk_num = (int)(floor((pointer+1) / (double) BLOCK_SIZE)); //block number in memory 
    int blk_ptr = pointer - ((mem_blk_num)*BLOCK_SIZE);     //pointer within block
    int data_left = length; //data left to write  (length - buf_offset)
    int data_written = 0;     //data written in current iteration (full block, or full block - blk_ptr or full block - blk_ptr)

    while (1){// keep looping until there is no data left to write
        if(LOG){printf("\n-> STARTING WRITE LOOP, inode_num = %d, mem_blk_num = %d, buf_offset = %d, data_left = %d, pointer = %d, blk_ptr = %d\n",inode_num,mem_blk_num,buf_offset,data_left, pointer, blk_ptr);}    
        if (mem_blk_num == 12){ //implement indirect pointer data block;

            //get disk block number, if unassigned create new 
            read_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);
            int ind_blk_num = cur_inode->pointers[12];

            if (ind_blk_num > 0){ //if ind_blk_num already exists, retrieve indirect pointer
                //retrieve block from disk, to get access to indirect pointers
                read_blocks(data_loc + ind_blk_num, 1, indirect_ptrs_mem);
                indirect_ptrs_t *indirect_ptrs = (indirect_ptrs_t *)indirect_ptrs_mem; //type cast
                disk_blk_num = indirect_ptrs[0].ptr; //assign value and exit 

                if (disk_blk_num == 0){ //if not assigned yet, assign
                    disk_blk_num = find_free_block(); 
                    indirect_ptrs[0].ptr = disk_blk_num; //assign 
                    write_blocks(data_loc + ind_blk_num, 1, indirect_ptrs);      //write back into memory
                }

            }else{ //if ind_blk_num does not exist, create it and create new indirect pointer
                ind_blk_num = find_free_block(); 
                cur_inode->pointers[12] = ind_blk_num;
                write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem); //write inode to mem

                read_blocks(data_loc + ind_blk_num, 1, indirect_ptrs_mem); //read block (should be null since unused)
                indirect_ptrs_t *indirect_ptrs = (indirect_ptrs_t *)indirect_ptrs_mem; //type cast
                disk_blk_num = find_free_block();   //find a block for the first pointer
                indirect_ptrs[0].ptr = disk_blk_num;    //assign to indirect ptrs
                write_blocks(data_loc + ind_blk_num, 1, indirect_ptrs);      //write back into disk  
            }

        }else if(mem_blk_num > 12){ //add to indirect pointers

            read_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);
            int ind_blk_num = cur_inode->pointers[12]; //get indirect block number

            read_blocks(data_loc + ind_blk_num, 1, indirect_ptrs_mem); //read from disk
            indirect_ptrs_t *indirect_ptrs = (indirect_ptrs_t *)indirect_ptrs_mem; //type cast
            disk_blk_num = indirect_ptrs[mem_blk_num-12].ptr; //retrieve value and continue program 
            if (disk_blk_num == 0) { //if non existent, create new, assign disk_blk_num and write back into disk
                disk_blk_num = find_free_block();
                indirect_ptrs[mem_blk_num-12].ptr = disk_blk_num;
                write_blocks(data_loc + ind_blk_num, 1, indirect_ptrs_mem); //read from disk
            }      
        }else{ //direct pointers    
            read_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);       //convert memory block number from inode into disk block number
            disk_blk_num = cur_inode->pointers[mem_blk_num];
          
            if (disk_blk_num == 0){               //if block is unassigned, assign a new one looking at free bit map(fbm)
                disk_blk_num = find_free_block();

                if (disk_blk_num <= 0){                                              //if a free block has not been found return -1
                    if(LOG){printf("-> free block has not been found \n");}
                    ofdt[fd].offset = pointer;                                      //update pointer in ofdt table
                    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem;  //update pointer/file size in inode 
                    inode_t *cur_inode = (inode_t *)&inode_table[inode_num];   
                    cur_inode->size = pointer;
                    //write inode into memory
                    write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);   
                    printf("free block has not been found\n");
                    return buf_offset;                                        //return data written until now
                }
                cur_inode->pointers[mem_blk_num] = disk_blk_num;                //update inode pointer
                write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);     //write into memory
            }
        }
        if (disk_blk_num > FILE_SYST_SIZE - data_loc - FBM_SIZE ){ // check for space in memory
            if(LOG){printf("-> No more space in memory %d, buf_offset : %d \n", pointer,buf_offset);}
            //update pointer in ofdt table
            ofdt[fd].offset = pointer;
            //update pointer/file size in inode 
            inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
            inode_t *cur_inode = (inode_t *)&inode_table[inode_num];   
            cur_inode->size = pointer;
            //write inode into memory
            write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);   
            return buf_offset;
        }
        read_blocks(data_loc + disk_blk_num, 1, data_blk_mem);   //read data block from disk
        data_t *data_blk = (data_t *)data_blk_mem;               //convert into byte addressable data type
        
        //data written in this iteration
        data_written = min(BLOCK_SIZE - blk_ptr, data_left);  //data that will be written to the block
        
        if (LOG){printf("data written this iteration : min(%d,%d) \n ",BLOCK_SIZE - blk_ptr,data_left);}
        
        // copy data
        memcpy(data_blk + blk_ptr, buf+buf_offset, data_written);
        write_blocks(data_loc + disk_blk_num, 1, data_blk_mem);     //save data block to memory 

        pointer = pointer + data_written;           //update pointer
        data_left = data_left - data_written;       //data left to write
        buf_offset = length - data_left;            //offset within buffer
        
        if (data_left <= 0){  //check if data has been writteN
            if(LOG){printf("-> HURRAY. no more data left to write, exiting loop,pointer : %d, buf_offset : %d \n", pointer,buf_offset);}
            //update pointer in ofdt table
            ofdt[fd].offset = pointer;
            //update pointer/file size in inode 
            inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
            inode_t *cur_inode = (inode_t *)&inode_table[inode_num];   
            cur_inode->size = pointer;
            //write inode into memory
            write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);   
            return buf_offset;  //exit loop 
        }
        
        //update iteration variables 
        mem_blk_num = mem_blk_num+1;                //increment memory block number 
        data_written = 0;                           //initialize to zero for next block
        blk_ptr = 0;                                //starting in a new block, so pointer within block will be 0

    }
}

int sfs_fread(int fd, char *buf, int length){
    //check fd validity
    if (fd_valid(fd)<0){
        return -1;
    }
    //retrieve inode number and pointer from ofdt
    int inode_num = ofdt[fd].inode;
    int pointer = ofdt[fd].offset;

    if(LOG){printf("\n\n-> READING %d bytes from inode_num : %d, which  has offset : %d \n",length,inode_num,pointer);}  

    // //retrieve inode 
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; //first cast to inode table
    inode_t *cur_inode = (inode_t *)&inode_table[inode_num];
    int filesize = cur_inode->size;  
    int size  = min(filesize-pointer,length); //size of data portion to write
    //loop variables
    int data_left = size;//data left to read  
    int disk_blk_num = 0;

    int buf_offset = 0; //at the start of buffer
    int mem_blk_num = (int)(floor((pointer+1) / (double) BLOCK_SIZE)); //floor division pointer/block size => block number
    int blk_ptr = pointer - ((mem_blk_num)*BLOCK_SIZE);               //pointer within block

    while(1){
        if(LOG){printf("\n-> STARTING READ LOOP, inode_num = %d, mem_blk_num = %d, buf_offset = %d, data_left = %d, pointer = %d, blk_ptr = %d\n",inode_num,mem_blk_num,buf_offset,data_left, pointer, blk_ptr);}   
        if (mem_blk_num < 12){ //direct pointers  

            disk_blk_num = cur_inode->pointers[mem_blk_num];    //convert memory block number from inode into disk block number
       
        }else{ //indirect pointers
            int ind_blk_num = cur_inode->pointers[12]; //get indirect block number
            read_blocks(data_loc + ind_blk_num, 1, indirect_ptrs_mem); //read from disk
            indirect_ptrs_t *indirect_ptrs = (indirect_ptrs_t *)indirect_ptrs_mem; //type cast
            disk_blk_num = indirect_ptrs[mem_blk_num-12].ptr; //retrieve value and continue program 
        }
       
        if (disk_blk_num == 0){  // check that disk block number is valid
            //assume reading has reached the end of the file 
            if (LOG){printf("-> No more blocks to read, pointer : %d, buf_offset: %d \n ",pointer,buf_offset);}
            ofdt[fd].offset = pointer+1;  //update pointer in ofdt table
            return buf_offset;  //exit loop 
        }

        //read block from disk
        read_blocks(data_loc + disk_blk_num, 1, data_blk_mem);
        data_t *data_blk = (data_t *)data_blk_mem; //convert into byte addressable data type

        //data read
        int data_read = min(BLOCK_SIZE - blk_ptr, data_left);
        if (LOG){printf("data read : min(%d,%d) \n ",BLOCK_SIZE - blk_ptr,data_left);}

        //copy data
        memcpy(buf+buf_offset, data_blk + blk_ptr, data_read);
        pointer = pointer + data_read;   //update pointer value
        data_left = data_left - data_read;          //data left to read
        buf_offset = size - data_left;        //offset within buffer
        //check if we are finished, if so break out of the loop
        if (data_left <= 0){  
            if (LOG){printf("->HURRAY. finished  reading, pointer : %d, buf_offset: %d \n ",pointer+1,buf_offset);}
            ofdt[fd].offset = pointer;  //update pointer in ofdt table, +1 because length = sizeof(buffer)+1
            return buf_offset;          //exit loop 
        }   

        //updat looping variables
        mem_blk_num = mem_blk_num+1;                //increment memory block number 
        blk_ptr = 0;                                //starting in a new block, so pointer within block will be 0
        data_read = 0;                              //initialize to 0 for next block
       
    }
}

//remove file from the file syst
int sfs_remove(char *fn){
    if (LOG){printf("-> Removing filename :  %s \n", fn);}
    int inode_num = 0;
    int dir_entry_num = 0;
    //remove file from directory entry and retrieve inode number
    dir_entry_t *dir = (dir_entry_t *)dir_mem; 
    for (int i = 0; i < MAX_FILE_NUM; i++){  
        if (strcmp(dir[i].filename,fn) == 0){
            inode_num = dir[i].inode;
            memset(dir[i].filename, '\0', sizeof(dir[i].filename));
            dir[i].inode = 0;
            dir_entry_num = i;
            break; //break out of loop
        }
    }
    //check if file is currently open in ofdt, if so return error -1
    for (int y = 0; y<MAX_FILE_NUM; y++){
        if (ofdt[y].inode == inode_num){ //if inode is in ofdt, return error
            return -1;
        }
    }
    write_dir_to_memory(dir_entry_num);             // write to memory selecting appropriate data block
    fbm_map_t *fbm_map = (fbm_map_t *)fbm_map_mem; //free bit map
    
    //retrieve inode block 
    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; 
    inode_t *cur_inode = (inode_t *)&inode_table[inode_num];   

    cur_inode->link_cnt = 0;
    cur_inode->size = 0;
    cur_inode -> mode = 0;
    for (int x = 0; x<12; x++){
        if (cur_inode->pointers[x] != 0){ //free used data blocks
            int datablk_index = cur_inode->pointers[x]; 
            int fbm_index = datablk_index + 1 + INODE_TBL_SIZE; //conversion between inode pointers and fbm indices
            fbm_map[fbm_index].available = 1; //free data block
        }
    }
    write_blocks(inodetbl_loc, INODE_TBL_SIZE, inode_tbl_mem);     //write inode into memory
    write_blocks(fbm_loc, FBM_SIZE, fbm_map_mem);     //write fbm into memory
    return 1;
}

//gets next filename in directory
int sfs_getnextfilename(char *fn){
    dir_entry_t *dir = (dir_entry_t *)dir_mem; //retrieve directory
    if (dir[current_file].inode == 0){ //if not in use, return 0
        current_file = 0;  //reset counter
        return 0;
    }
    strcpy(fn,dir[current_file].filename); //copy string
    current_file = current_file+1; 
    return 1;
}


//returns filesize 
int sfs_getfilesize(const char* fn){ 
    int inode_num = 0;
    int size = 0;
    dir_entry_t *dir = (dir_entry_t *)dir_mem; //retrieve directory
    for (int i = 0; i < MAX_FILE_NUM; i++){  
        if (strcmp(dir[i].filename,fn) == 0){
            inode_num = dir[i].inode;
            if (inode_num == 0) {
                return -1;
            }
            break; //break out of loop
        }
    }

    inode_table_t *inode_table = (inode_table_t *)inode_tbl_mem; 
    inode_t *cur_inode = (inode_t *)&inode_table[inode_num];  
    size = cur_inode->size;
    return size;
}



//run with gcc -lm for floor fct

