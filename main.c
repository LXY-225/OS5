#include<stdlib.h>
#include "stdio.h"
#include "disk.h"
#include<string.h>

typedef unsigned int       uint32_t;
typedef unsigned short     int uint16_t;
typedef unsigned          char uint8_t;

const uint16_t file = 0;
const uint16_t dir = 1;

const uint8_t file_ = 0;
const uint8_t dir_ = 1;

const int half_blk_size = 512;

char buf_super[1024];     // 用于读写磁盘
char buf_inode[1024];   
char buf_block[1024];
char buf_inode_src[1024];
char buf_block_src[1024];

// 超级块
typedef struct super_block {
    int32_t magic_num;                  // 幻数
    int32_t free_block_count;           // 空闲数据块数
    int32_t free_inode_count;           // 空闲inode数
    int32_t dir_inode_count;            // 目录inode数
    uint32_t block_map[128];            // 数据块占用位图
    uint32_t inode_map[32];             // inode占用位图
} sp_block;

struct inode {
    uint32_t size;              // 文件大小
    uint16_t file_type;         // 文件类型（文件/文件夹）
    uint16_t link;              // 连接数
    uint32_t block_point[6];    // 数据块指针                 /// 数据块号是32位,一个数据块1KB,假设每个文件大小不会超过6KB
};

// 目录项
struct dir_item {               // 目录项一个更常见的叫法是 dirent(directory entry)
    uint32_t inode_id;          // 当前目录项表示的文件/目录的对应inode
    uint16_t valid;             // 当前目录项是否有效 
    uint8_t type;               // 当前目录项类型（文件/目录）
    char name[121];             // 目录项表示的文件/目录的文件名/目录名
};


/*
* i : 分配的inode号
* j : 分配的block号
* ptr_sp : 已经读入内存的 超级块的 头指针
* 功能 : 修改对应的两个map 却未写回磁盘
*/
void set_inode_block_map(int i, int j, sp_block * ptr_sp){   // 刚刚从磁盘读到的超级块的头指针 ptr_sp
    int x, y;
    uint32_t value;

    if(ptr_sp != NULL){
        if(i != -1){
            x = i / 32; 
            y = i % 32;         // 在[x][y] 设为1
            value = 1 << (32-y-1);
            
            ptr_sp->inode_map[x] |= value;

            // printf("置位之后，ptr_sp->inode_map[%d] = %x\n", x, ptr_sp->inode_map[x]);
        }
        

        if(j != -1){    // 表示不同时修改block占用位时
            x = j / 32; 
            y = j % 32;         // 在[x][y] 设为1
            value = 1 << (32-y-1);
            
            ptr_sp->block_map[x] |= value;
            // printf("置位之后，ptr_sp->block_map[%d] = %x\n", x, ptr_sp->block_map[x]);
        }
    }else{
        printf("empty pointer!\n");
        exit(0);
    }
}

/*
* 功能: 实现查找内存中的超级块的map部分，找到空闲的inode块，返回inode号
*/
uint32_t find_free_inode(sp_block *p_sp){
    uint32_t inode_index;

    if(p_sp == NULL){
        printf("find_free_inode input pointer is empty\n");
        exit(0);
    }
    if(p_sp->free_inode_count == 0){
        printf("没有空闲的inode，创建文件失败\n");
        exit(0);
    }else{
        // 找到一个空闲inode
        // 数据左移1位 和 最高位为1的数相与
        uint32_t x = 0x80000000;
        uint32_t value;
        
        int flag = 0;

        for(int i = 0; i < 32; i++){
            value = p_sp->inode_map[i];
            if(value != 0xffffffff){
                for(int j = 0; j < 32; j++, value <<= 1){  
                    if(!(value & x)){                    // 被占用的bit是1，空闲是0
                        
                        inode_index = i * 32 + j;
                        flag = 1;
                        printf("find free inode 号: %d\n", inode_index);
                        break;
                    }
                }
            }
            if(flag == 1)
                break;
        }
    }
    return inode_index;
}

/*
* 功能: 实现查找内存中的超级块的map部分，找到空闲的block块，返回block号
*/
uint32_t find_free_block(sp_block *p_sp){
    uint32_t block_index;

    if(p_sp == NULL){
        printf("find_free_inode input pointer is empty\n");
        exit(0);
    }
    if(p_sp->free_block_count == 0){
        printf("没有空闲的block，创建文件失败\n");
        exit(0);
    }else{
        uint32_t x = 0x80000000;
        uint32_t value;

        int flag = 0;
        for(int i = 0; i < 128; i++){
            value = p_sp->block_map[i];

            if(value != 0xffffffff){
                for(int j = 0; j < 32; j++, value <<= 1){  
                    if(!(value & x)){                    // 被占用的bit是1，空闲是0
                        block_index = i * 32 + j;
                        
                        flag = 1;

                        printf("find free block 号: %d\n", block_index);
                        break;
                    }
                }
            }
            if(flag == 1)
                break;
        }
    }
    return block_index;
}

/*
* input: 超级块头指针 ptr_sp
* 完成: 超级块初始化、建立根目录 （并写回磁盘）
*/
void init(sp_block * ptr_sp){      

    if(ptr_sp == NULL){
        printf(" empty init pointer!\n");
        exit(0);
    }
    ptr_sp->magic_num = 0xdec0de;       // 幻数赋值

    ptr_sp->free_block_count = 4063 - 1;    // 4063个数据块  - 1, 0号块不用了  
    ptr_sp->free_inode_count = 32;     // 32个inode块
    ptr_sp->dir_inode_count = 0;

    printf("执行简单赋值完成\n");
    ptr_sp->block_map[0] = 0x80000000;       // 直接把0号block置位，设为已用
    for(int i = 1; i < 128; i++){
        ptr_sp->block_map[i] = 0;
    }
    for(int i = 0; i < 32; i++){
        ptr_sp->inode_map[i] = 0;
    }


    // 建立根目录

    disk_read_block(2, buf_inode);   // 读取2、3号磁盘块
    disk_read_block(3, buf_inode + half_blk_size);

    struct inode * ptr_inode = (struct inode *)buf_inode;
    ptr_inode += 2;     // 找到inode[2]的位置

    ptr_inode->size = 0;
    ptr_inode->file_type = dir;
    ptr_inode->link = 1;
    ptr_inode->block_point[0] = 2;     // 根目录
    for(int i = 1; i < 6; i++){
        ptr_inode->block_point[i] = 0;
    }

    // 超级块更新
    
    ptr_sp->free_inode_count--;   
    ptr_sp->dir_inode_count++;     // inode 0 被占用作为根目录/ 的 inode
    
    ptr_sp->free_block_count--;    // block 2 被占用

    // 将2号inode，2号block设为占用
    set_inode_block_map(2, 2, ptr_sp);   // 继续从刚刚读出的 进行数据修改，之后一次写回磁盘
    
    disk_write_block(0, buf_super);    // 写回超级块
    disk_write_block(1, buf_super + half_blk_size);

    disk_write_block(2, buf_inode);     
    disk_write_block(3, buf_inode + half_blk_size);

}

// 将i号block转为磁盘地址号
uint32_t trans_block_num(uint32_t i){
    return (i+33)*2;
}

// 将i号inode转为磁盘地址号（整体的号，不是inode下标）
uint32_t trans_inode_num(uint32_t i){
    return (i+1)*2;
}



/*
* str : 即分解后的 ls命令里的每段路径
* deep: 即路径数 (eg. ls /home ：deep = 1)
* 功能: 完成 ls 命令
*/
void ls_ok(char str[10][20], int deep){
    int cur_deep = 0;  

    int flag = 0;

    disk_read_block(2, buf_inode);                           // 读出根目录inode
    disk_read_block(3, buf_inode + half_blk_size);

    struct inode * p_inode = (struct inode *)buf_inode;
    p_inode += 2;       // 来到根目录的inode

    struct dir_item * p_dir;
    uint32_t num;

    int index;
    int index_i, index_j;

    while(cur_deep < deep){
        if(p_inode->file_type == dir){ // inode 显示为 目录

            for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
                if(p_inode->block_point[i] != 0){
                    num = p_inode->block_point[i];      // 是 num号block！！！         真实修改的目录项在block num中！！！修改在buf_block

/*-------------------------------------------  将根目录中对应的block读入到 buf_block中！！！--------------------------------------------*/
                    disk_read_block(trans_block_num(num), buf_block);                   
                    disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                    // finish 将对应 block完整读入

                    // 读入的是目录     遍历目录找到一个空闲位置，看看有没有
                    p_dir = (struct dir_item *)buf_block;
                    for(int j = 0; j < 8; j++){

                        if(strcmp(p_dir[j].name, str[cur_deep]) == 0){        // 找到第一层文件夹       去匹配
                            // printf("在根目录下找到folder的inode\n");
                            index = p_dir[j].inode_id;
                            index_i = index/32;
                            index_j = index % 32;
                            disk_read_block(trans_inode_num(index_i), buf_inode);
                            disk_read_block(trans_inode_num(index_i) + 1, buf_inode + half_blk_size);

                            p_inode = (struct inode *)buf_inode;
                            p_inode += index_j;

                            // printf("p_inode真正指向了内存中 %d 号 inode\n", index_i * 32 + index_j);
                            flag = 1;
                            break;
                        }
                    
                    }
                }
                if(flag)
                    break;
            }

            if(flag == 0){
                printf("第 %d 层文件夹未找到\n", cur_deep);
                exit(0);
            }
        }else{
            printf("error:路径中包含文件, 应该均为文件夹\n");
            exit(0);
        }
        cur_deep ++;      // 又进入一层文件夹
        
    }

/// 找文件夹循环出来之后

    printf("文件夹下内容如下:\n");
    if(p_inode->file_type == dir){ // inode 显示为 目录，一会读出的block就是一堆目录项

        for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
            if(p_inode->block_point[i] != 0){
                uint32_t num = p_inode->block_point[i];      // 是 num号block

                disk_read_block(trans_block_num(num), buf_block);                         // 将 block读出
                disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                // finish 将对应 block完整读入

                // 读入的是目录
                p_dir = (struct dir_item *)buf_block;
                
                for(int j = 0; j < 8; j++){
                    if(p_dir[j].valid == 1){
                        printf("%s\n", p_dir[j].name);
                    }
                }  
            }
        }
        
    }
}

/* 传入文件名，查看在根目录下是否有同名文件 或 同名文件夹*/          // 只是读了磁盘，没有修改，不用写回
int ls_root_judge(char *name, uint16_t type){

    disk_read_block(2, buf_inode);                           // 读出根目录inode
    disk_read_block(3, buf_inode + half_blk_size);

    struct inode * p_inode = (struct inode *)buf_inode;
    p_inode += 2;       // 来到根目录的inode

    if(p_inode->file_type == dir){ // inode 显示为 目录，一会读出的block就是一堆目录项

        for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
            if(p_inode->block_point[i] != 0){
                uint32_t num = p_inode->block_point[i];      // 是 num号block

                disk_read_block(trans_block_num(num), buf_block);                         // 将 block读出
                disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                // finish 将对应 block完整读入

                // 读入的是目录
                struct dir_item * p_dir = (struct dir_item *)buf_block;
                
                for(int j = 0; j < 8; j++){
                    
                    if(strcmp(p_dir[j].name, name) == 0 && p_dir[j].type == type){         // 遍历，发现有同名文件 或者同名文件夹
                        return 1;       // 返回：不可创建该文件（夹）
                    }
                }
                  
            }
        }
        
    }
    return 0;
}

// 判断某个文件（夹）是否已经存在
int ls_judge(char str[10][20], int deep){
    int cur_deep = 0;  

    int flag = 0;

    disk_read_block(2, buf_inode);                           // 读出根目录inode
    disk_read_block(3, buf_inode + half_blk_size);

    struct inode * p_inode = (struct inode *)buf_inode;
    p_inode += 2;       // 来到根目录的inode

    struct dir_item * p_dir;
    uint32_t num;

    int index;
    int index_i, index_j;

    while(cur_deep < deep){
        if(p_inode->file_type == dir){ // inode 显示为 目录

            for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
                if(p_inode->block_point[i] != 0){
                    num = p_inode->block_point[i];      // 是 num号block！！！         真实修改的目录项在block num中！！！修改在buf_block

/*-------------------------------------------  将根目录中对应的block读入到 buf_block中！！！--------------------------------------------*/
                    disk_read_block(trans_block_num(num), buf_block);                   
                    disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                    // finish 将对应 block完整读入

                    // 读入的是目录     遍历目录找到一个空闲位置，看看有没有
                    p_dir = (struct dir_item *)buf_block;
                    for(int j = 0; j < 8; j++){

                        if(strcmp(p_dir[j].name, str[cur_deep]) == 0){        // 找到第一层文件夹       去匹配
                            // printf("在根目录下找到folder的inode\n");
                            index = p_dir[j].inode_id;
                            index_i = index/32;
                            index_j = index % 32;
                            disk_read_block(trans_inode_num(index_i), buf_inode);
                            disk_read_block(trans_inode_num(index_i) + 1, buf_inode + half_blk_size);

                            p_inode = (struct inode *)buf_inode;
                            p_inode += index_j;

                            // printf("p_inode真正指向了内存中 %d 号 inode\n", index_i * 32 + index_j);
                            flag = 1;
                            break;
                        }
                    
                    }
                }
                if(flag)
                    break;
            }

            if(flag == 0){
                printf("第 %d 层文件夹未找到\n", cur_deep);
                // exit(0);
                return -1;
            }
        }else{
            printf("error:路径中包含文件, 应该均为文件夹\n");
            exit(0);
        }
        cur_deep ++;      // 又进入一层文件夹
        
    }

/// 找文件夹循环出来之后

    printf("文件夹下内容如下:\n");
    if(p_inode->file_type == dir){ // inode 显示为 目录，一会读出的block就是一堆目录项

        for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
            if(p_inode->block_point[i] != 0){
                uint32_t num = p_inode->block_point[i];      // 是 num号block

                disk_read_block(trans_block_num(num), buf_block);                         // 将 block读出
                disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                // finish 将对应 block完整读入

                // 读入的是目录
                p_dir = (struct dir_item *)buf_block;
                
                for(int j = 0; j < 8; j++){
                    if(p_dir[j].valid == 1 && strcmp(p_dir[j].name, str[cur_deep]) == 0){
                        // printf("%s\n", p_dir[j].name);
                        return 1;
                    }
                }  
            }
        }
        
    }
    return 0;
}



// 判断是否还有已经分配的block可以给一个目录项，如果有,已经读出来在buf_block

// 假设：文件没有大于6KB的，所以不会出现block全部占满的情况  （说的不是很准确）

/*
* p : 读入内存的inode指针
* return : 0 需要新分配一个block，1 不需要新分配block 且 空闲block已经读入buf_block
* find_index : 用于返回找到的空闲项的下标
* write_num : 用于返回写回磁盘的block号
*/
int judge_need_allo_block(struct inode * p, int * find_index, int * write_num){
    // 找到一个空闲项便可返回
    int i = 0, j = 0;
    int block_num;
    int flag = 0;
    if(p != NULL){
        for(i = 0; i < 6; i++){
            if(p->block_point[i] != 0){
                block_num = p->block_point[i];

                disk_read_block(trans_block_num(block_num), buf_block);                   
                disk_read_block(trans_block_num(block_num)+1, buf_block + half_blk_size);

                *write_num = block_num;      // 用于调用函数的 写回磁盘

                struct dir_item * p_dir = (struct dir_item *)buf_block;
                for(j = 0; j < 8; j++){
                    if(p_dir[j].valid == 0){
                            // 找到了，可以放 要创建文件的 目录项
                        *find_index = j;           // 用于被调函数 直接使用 空闲 目录项j
                        // printf("在judge函数中，找到了可用的目录项,位置是:\n");

                        // printf("find_index = %d\n", *find_index);
                        // printf("write_num = %d\n", *write_num);
                        flag = 1;
                        
                        break; 
                    }
                }
            }
            if(flag)
                break;
        }

    }else{
        printf("pointer p in judge_need_allo_block is empty!\n");
        exit(0);
    }

    return flag;
}



// 任意目录下新建文件夹     确保没有同名文件
void create_folder_ok(char str[10][20], int deep){

    int re = ls_judge(str, deep);
    
    int cur_deep = 0;

    disk_read_block(0, buf_super);
    disk_read_block(1, buf_super + half_blk_size);
    sp_block *p_sp = (sp_block *)buf_super;                  // 已经将超级块读出来！！！！！！！

    if(p_sp->free_inode_count == 0){
        printf("没有空闲的inode，创建文件失败\n");
        exit(0);
    }else if(ls_root_judge("folder", 1) == 1){                      // 看能不能重复在根目录下建立a.c
        printf("该目录下已有相关文件（夹），创建文件失败\n");
        exit(0);
    
    }else if(re == 1){                      // 不能重复在某个目录下多次建立a.c
        printf("该目录下已有相关文件（夹），创建文件失败\n");
        exit(0);
    }else if(re == -1){
        printf("文件路径中有不存在的部分\n");
        exit(0);
    }else{
        // 找到一个空闲inode
        // 数据左移1位 和 最高位为1的数相与
        uint32_t x = 0x80000000;
        uint32_t value;
        uint32_t inode_index, block_index;
        uint32_t inode_i, inode_j;
        uint32_t block_i, block_j;
        uint32_t num;

        int flag = 0;

        inode_index = find_free_inode(p_sp);
        inode_i = inode_index / 32;
        inode_j = inode_index % 32;

/*---------------------------------printf("找到空闲inode: %d\n", inode_index); -------------------------------------------*/

        block_index = find_free_block(p_sp);
        block_i = block_index / 32;
        block_j = block_index % 32;
        
/*-------------------------------------printf("找到空闲block: %d\n", block_index);------------------------------------------------*/

        
        // 找到了号：inode_index
        // 将超级块修改完成           后面文件夹要用的inode
        p_sp->free_inode_count--;
        p_sp->free_block_count--;
        p_sp->dir_inode_count++;       // inode是文件夹的inode
        set_inode_block_map(inode_index, block_index, p_sp);   // 如何判断是否真的：完成了置位？为什么一直返回0？

        // printf("完成超级块在内存的修改\n");
        printf("已经内存中超级块修改了 free_inode 和 free_block\n");
        printf("完成了置位：%d号inode 和 %d号block\n", inode_index, block_index);


/*------------------------------------------ 将根目录读出来！！！读到buf_inode中 ------------------------------------------------------------------*/                                                                 
        

        flag = 0;

        disk_read_block(2, buf_inode);
        disk_read_block(3, buf_inode + half_blk_size);

        struct inode * p_inode = (struct inode *)buf_inode;
        p_inode += 2;       // 来到根目录的inode

        printf("p_inode指向了根目录inode\n");
        int number_real_hard_inode = 2;  // 要写回的磁盘号


        /*-------循环开始--------------------------*/
        while(cur_deep < deep){
            printf("cur_deep = %d\n", cur_deep);
            flag = 0;
            if(p_inode->file_type == dir){ // inode 显示为 目录

                for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
                    if(p_inode->block_point[i] != 0){
                        num = p_inode->block_point[i];      // 是 num号block      真实修改的目录项在block num中！！！修改在buf_block

    /*-------------------------------------------  将根目录中对应的block读入到 buf_block中--------------------------------------------*/
                        disk_read_block(trans_block_num(num), buf_block);                   
                        disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                        // printf("将 %d 号 block 读入buf_block\n",num);
                        // finish 将对应 block完整读入

                        // 读入的是目录     遍历目录找到一个空闲位置，看看有没有
                        struct dir_item * p_dir = (struct dir_item *)buf_block;
                        for(int j = 0; j < 8; j++){

                            if(strcmp(p_dir[j].name, str[cur_deep]) == 0){        // 找到第一层文件夹       去匹配
                                // printf("在创建/folder/b.c的过程中:\n");
                                printf("找到了文件夹对应的inode\n:");
                                puts(str[cur_deep]);

                                int index = p_dir[j].inode_id;

                                int index_i = index / 32;
                                int index_j = index % 32;
                            
                                disk_read_block(trans_inode_num(index_i), buf_inode);
                                disk_read_block(trans_inode_num(index_i) + 1, buf_inode + half_blk_size);

                                // printf("把 %d 号 inode 读到 buf_inode中\n", index);
                                number_real_hard_inode = trans_inode_num(index_i);

                                // 相当于 一个轮回，找到目录的Inode
                                p_inode = (struct inode *)buf_inode;
                                p_inode += index_j;

                                printf("当前p_inode指向 inode号为: %d\n", index);
                                // 真正指向了内存中 文件夹demo的 inode
                                flag = 1;
                                // printf("p_inode重新指向新的一层inode的位置\n");
                                break;         // 可以匹配深一层的文件夹了
                            }
                        
                        }
                    }
                    if(flag)
                        break;
                }

                if(flag == 0){
                    printf("第 %d 层文件夹未找到\n", cur_deep);
                    exit(0);
                }
            }else{
                printf("error: 路径中含有文件\n");
                exit(0);
            }
            cur_deep ++;      // 又搞了一层文件夹

        }

/// 相当于找文件夹循环出来之后
        if(p_inode->file_type == dir){ // inode 显示为 目录
            printf("开始建立文件夹了\n");
            int find_index;
            int write_block_index;
            if(judge_need_allo_block(p_inode, &find_index, &write_block_index) == 1){
                // 找到合适的一项在 buf_block[find_index], buf_block要写回 block号为write_block_index的磁盘中
                printf("找到可用目录项：buf_block[%d] , 写回 %d 号磁盘\n", find_index, write_block_index);
                struct dir_item * p_dir = (struct dir_item *)buf_block;

                p_dir[find_index].valid = 1;                            // 完成目录项
                memcpy(p_dir[find_index].name, str[cur_deep], strlen(str[cur_deep]));
                p_dir[find_index].type = dir_;
                p_dir[find_index].inode_id = inode_index;

                num = write_block_index;                           /// 要写回的block号
                // printf("完成目录项的设置3\n");

            }else{
                // 没有找到空闲block
                printf("没有空闲block\n");
                uint32_t new_block_index = find_free_block(p_sp);     // 又找到一个新的block
                
                // 即将分配block号为 new_block_index , 修改超级块 ！！！！！！又分配一个block
                p_sp->free_block_count--;
                set_inode_block_map(-1, new_block_index, p_sp);


                for(int k = 0; k < 6; k++){
                    if(p_inode->block_point[k] == 0){
                        // 将其用上, 目录上新分配一个块 new_block_index
                        p_inode->block_point[k] = new_block_index;       // 修改了p_inode的内容，就要写回去

                        disk_write_block(number_real_hard_inode, buf_inode);                      // 在目录下新分配一个块，目录也进行了修改，要写回！！
                        disk_write_block(number_real_hard_inode + 1, buf_inode + half_blk_size);


                        
                        // printf("根目录增加了一个块指向\n");
                        // printf("p_inode->block_point[%d] = %d\n", k, p_inode->block_point[k]);
                        



                        // 把new_block_index号block块读入内存
                        disk_read_block(trans_block_num(new_block_index), buf_block);
                        disk_read_block(trans_block_num(new_block_index) + 1, buf_block + half_blk_size);

                        struct dir_item * p_dir = (struct dir_item *)buf_block;
                        p_dir[0].valid = 1;
                        memcpy(p_dir[0].name, str[0], strlen(str[0]));
                        p_dir[0].type = dir_;
                        p_dir[0].inode_id = inode_index;

                        num = new_block_index;                     /// 要写回的block号
                        // printf("完成目录项的设置4\n");
                        break;
                    }
                }
            
            }

            // 将num号block写回磁盘
            disk_write_block(trans_block_num(num), buf_block);
            disk_write_block(trans_block_num(num)+1, buf_block + half_blk_size);

        }

        // printf("完成目录项的设置，也指向了inode\n");
        flag = 0;


/*--------------------------------将要被分配的inode读出来！！！（可能和目录所在inode一样，上面先写入）---------------------------------------*/

        char buf_allo_inode[1024];
        disk_read_block(trans_inode_num(inode_i), buf_allo_inode);        // inode在第inode_i号block里
        disk_read_block(trans_inode_num(inode_i) + 1, buf_allo_inode + half_blk_size);

        struct inode * p_allo_inode = (struct inode *)buf_allo_inode;
        p_allo_inode += inode_j;

        // 找到了要改的inode
        p_allo_inode->size = 0;
        p_allo_inode->file_type = dir;
        p_allo_inode->link = 1;
        // 给文件夹初始化一个block
        p_allo_inode->block_point[0] = block_index;     // inode 表明文件第一个块在block_inode号
    
        // printf("在根目录创建文件夹的过程中:\n");
        // printf("分配给文件夹的inode 号为： %d\n", inode_index);
        // printf("inode设置如下:\n");
        // printf("file_type = %d\n", p_allo_inode->file_type);



        // printf("完成inode的设置，指向block : %d\n", p_allo_inode->block_point[0]);
        
        disk_write_block(trans_inode_num(inode_i), buf_allo_inode);        // inode在第inode_i号block里
        disk_write_block(trans_inode_num(inode_i) + 1, buf_allo_inode + half_blk_size);
    

        // 开始写回磁盘
        disk_write_block(0, buf_super);
        disk_write_block(1, buf_super + half_blk_size);
    }

    
}

// 在根目录下新建文件  可以足够多
void create_file(char str[10][20]){
   
    disk_read_block(0, buf_super);
    disk_read_block(1, buf_super + half_blk_size);
    sp_block *p_sp = (sp_block *)buf_super;                  // 已经将超级块读出来！！！！！！！

    if(p_sp->free_inode_count == 0){
        printf("没有空闲的inode，创建文件失败\n");
        exit(0);
    }else if(p_sp->free_block_count == 0){
        printf("没有空闲的block，创建文件失败\n");
        exit(0);
    }
    else if(ls_root_judge(str[0], 0) == 1){                      // 看能不能重复在根目录下建立a.c
        printf("该目录下已有相关文件（夹），创建文件失败\n");
        exit(0);
    }
    else{
        // 找到一个空闲inode
        // 数据左移1位 和 最高位为1的数相与
        uint32_t x = 0x80000000;
        uint32_t value;
        uint32_t inode_index, block_index;
        uint32_t inode_i, inode_j;
        uint32_t block_i, block_j;
        uint32_t num;

        int flag = 0;

        inode_index = find_free_inode(p_sp);
        inode_i = inode_index / 32;
        inode_j = inode_index % 32;

/*---------------------------------printf("找到空闲inode: %d\n", inode_index); -------------------------------------------*/

        block_index = find_free_block(p_sp);
        block_i = block_index / 32;
        block_j = block_index % 32;
        
/*-------------------------------------printf("找到空闲block: %d\n", block_index);------------------------------------------------*/
        // 找到了号：inode_index 和 block_index
        // 将超级块修改完成
        p_sp->free_inode_count--;
        p_sp->free_block_count--;
        set_inode_block_map(inode_index, block_index, p_sp);   // 如何判断是否真的：完成了置位？为什么一直返回0？

        // printf("完成超级块在内存的修改\n");
        

/*------------------------------------------ 将根目录读出来！！！读到buf_inode中 ------------------------------------------------------------------*/                                                                 
        

        flag = 0;

        disk_read_block(2, buf_inode);
        disk_read_block(3, buf_inode + half_blk_size);

        struct inode * p_inode = (struct inode *)buf_inode;
        p_inode += 2;       // 来到根目录的inode

/// 相当于找文件夹循环出来之后
        if(p_inode->file_type == dir){ // inode 显示为 目录

            int find_index;
            int write_block_index;
            if(judge_need_allo_block(p_inode, &find_index, &write_block_index) == 1){
                // 找到合适的一项在 buf_block[find_index], buf_block要写回 block号为write_block_index的磁盘中

                struct dir_item * p_dir = (struct dir_item *)buf_block;

                p_dir[find_index].valid = 1;                            // 完成目录项
                memcpy(p_dir[find_index].name, str[0], strlen(str[0]));
                p_dir[find_index].type = file_;
                p_dir[find_index].inode_id = inode_index;

                num = write_block_index;                           /// 要写回的block号
                // printf("完成目录项的设置3\n");

            }else{
                // 没有找到空闲block
                uint32_t new_block_index = find_free_block(p_sp);     // 又找到一个新的block
                
                // 即将分配block号为 new_block_index , 修改超级块 ！！！！！！
                p_sp->free_block_count--;
                set_inode_block_map(-1, new_block_index, p_sp);


                for(int k = 0; k < 6; k++){
                    if(p_inode->block_point[k] == 0){
                        // 将其用上, 目录上新分配一个块 new_block_index
                        p_inode->block_point[k] = new_block_index;       // 修改了p_inode的内容，就要写回去

                        disk_write_block(2, buf_inode);                      // 在根目录下新分配一个块，根目录也进行了修改，要写回！！
                        disk_write_block(3, buf_inode + half_blk_size);

                        
                        // printf("根目录增加了一个块指向\n");
                        // printf("p_inode->block_point[%d] = %d\n", k, p_inode->block_point[k]);
                        

                        // 把new_block_index号block块读入内存
                        disk_read_block(trans_block_num(new_block_index), buf_block);
                        disk_read_block(trans_block_num(new_block_index) + 1, buf_block + half_blk_size);

                        struct dir_item * p_dir = (struct dir_item *)buf_block;
                        p_dir[0].valid = 1;
                        memcpy(p_dir[0].name, str[0], strlen(str[0]));
                        p_dir[0].type = file_;
                        p_dir[0].inode_id = inode_index;

                        num = new_block_index;                     /// 要写回的block号
                        // printf("完成目录项的设置4\n");
                        break;
                    }
                }
            
            }

            // 将num号block写回磁盘
            disk_write_block(trans_block_num(num), buf_block);
            disk_write_block(trans_block_num(num)+1, buf_block + half_blk_size);

        }

        // printf("完成目录项的设置，也指向了inode\n");
        flag = 0;


/*--------------------------------将要被分配的inode读出来！！！（可能和目录所在inode一样，上面先写入）---------------------------------------*/

        char buf_allo_inode[1024];
        // char buf_allo_block[1024];
        disk_read_block(trans_inode_num(inode_i), buf_allo_inode);        // inode在第inode_i号block里
        disk_read_block(trans_inode_num(inode_i) + 1, buf_allo_inode + half_blk_size);

        struct inode * p_allo_inode = (struct inode *)buf_allo_inode;
        p_allo_inode += inode_j;

        // 找到了要改的inode
        p_allo_inode->size = 0;
        p_allo_inode->file_type = file;
        p_allo_inode->link = 1;
        p_allo_inode->block_point[0] = block_index;     // inode 表明文件第一个块在block_inode号
    

        // printf("完成inode的设置，指向block : %d\n", p_allo_inode->block_point[0]);
        
        disk_write_block(trans_inode_num(inode_i), buf_allo_inode);        // inode在第inode_i号block里
        disk_write_block(trans_inode_num(inode_i) + 1, buf_allo_inode + half_blk_size);
    

        // 开始写回磁盘
        disk_write_block(0, buf_super);
        disk_write_block(1, buf_super + half_blk_size);
    
    }
}




// 任意路径下新建文件 eg. touch /folder/a.c
void create_file_ok(char str[10][20], int deep){
    int re = ls_judge(str, deep);
    int cur_deep = 0;

    disk_read_block(0, buf_super);
    disk_read_block(1, buf_super + half_blk_size);
    sp_block *p_sp = (sp_block *)buf_super;                  // 已经将超级块读出来！！！！！！！
    // printf("完成0号超级块读入 buf_super\n");

    if(p_sp->free_inode_count == 0){
        printf("没有空闲的inode，创建文件失败\n");
        exit(0);
    }else if(p_sp->free_block_count == 0){
        printf("没有空闲的block，创建文件失败\n");
        exit(0);
    
    }else if(re == 1){                      // 不能重复在某个目录下多次建立a.c
        printf("该目录下已有相关文件（夹），创建文件失败\n");
        exit(0);
    }else if(re == -1){
        printf("文件路径中有不存在的部分\n");
        exit(0);
    }else{
        // 找到一个空闲inode
        // 数据左移1位 和 最高位为1的数相与
        uint32_t x = 0x80000000;
        uint32_t value;
        uint32_t inode_index, block_index;
        uint32_t inode_i, inode_j;
        uint32_t block_i, block_j;
        uint32_t num;

        int flag = 0;

        inode_index = find_free_inode(p_sp);
        inode_i = inode_index / 32;
        inode_j = inode_index % 32;

/*---------------------------------printf("找到空闲inode: %d\n", inode_index); -------------------------------------------*/

        block_index = find_free_block(p_sp);
        block_i = block_index / 32;
        block_j = block_index % 32;
        
/*-------------------------------------printf("找到空闲block: %d\n", block_index);------------------------------------------------*/
        // 找到了号：inode_index 和 block_index
        // 将超级块修改完成
        p_sp->free_inode_count--;
        p_sp->free_block_count--;
        set_inode_block_map(inode_index, block_index, p_sp);   // 如何判断是否真的：完成了置位？为什么一直返回0？

        // printf("完成超级块在内存的初步修改，分配了 %d 号inode，%d 号 block\n", inode_index, block_index);
        

/*------------------------------------------ 将根目录读出来！！！读到buf_inode中 ------------------------------------------------------------------*/                                                                 
        

        flag = 0;

        disk_read_block(2, buf_inode);
        disk_read_block(3, buf_inode + half_blk_size);
        // printf("把 根目录的inode 读到 buf_inode中\n");

        struct inode * p_inode = (struct inode *)buf_inode;
        p_inode += 2;       // 来到根目录的inode
        // printf("p_inode指向 根目录对应的inode\n");

        int number_real_hard_inode = 2;   // 要初始化！！！
        
        // printf("cur_deep = %d, deep = %d\n", cur_deep, deep);
        

        while(cur_deep < deep){
            // printf("进入循环---------------------------\n");
            // printf("开始找 str[%d] = %s\n", cur_deep, str[cur_deep]);

            flag = 0;
            if(p_inode->file_type == dir){ // inode 显示为 目录

                for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
                    if(p_inode->block_point[i] != 0){
                        num = p_inode->block_point[i];      // 是 num号block！！！         真实修改的目录项在block num中！！！修改在buf_block

    /*-------------------------------------------  将根目录中对应的block读入到 buf_block中！！！--------------------------------------------*/
                        disk_read_block(trans_block_num(num), buf_block);                   
                        disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                        // printf("将 %d 号 block 读入buf_block\n",num);
                        // finish 将对应 block完整读入

                        // 读入的是目录     遍历目录找到一个空闲位置，看看有没有
                        struct dir_item * p_dir = (struct dir_item *)buf_block;
                        for(int j = 0; j < 8; j++){
                            // printf("p_dir[%d].name = %s\n", j, p_dir[j].name);
                            if(strcmp(p_dir[j].name, str[cur_deep]) == 0){        // 找到第一层文件夹       去匹配
                                // printf("找到了文件夹 %s 对应的inode:\n", str[cur_deep]);

                                int index = p_dir[j].inode_id;
                                int index_i = index / 32;
                                int index_j = index % 32;
                                // printf("index_i = %d\n", index_i);
                                // printf("index_j = %d\n", index_j);

                                disk_read_block(trans_inode_num(index_i), buf_inode);
                                disk_read_block(trans_inode_num(index_i) + 1, buf_inode + half_blk_size);

                                // printf("把 %d 号 inode 读到 buf_inode中\n", index);
                                number_real_hard_inode = trans_inode_num(index_i);

                                // 相当于 一个轮回，找到目录的Inode
                                p_inode = (struct inode *)buf_inode;
                                p_inode += index_j;

                                // printf("当前p_inode指向 inode号为: %d\n", index);
                                // 真正指向了内存中 文件夹demo的 inode
                                flag = 1;
                                // printf("即 完成 p_inode  指向新的更深一层inode的位置\n");
                                break;
                            }
                        
                        }
                    }
                    if(flag)
                        break;
                }

                if(flag == 0){
                    printf("第 %d 层文件夹未找到\n", cur_deep);
                    exit(0);
                }
            }else{
                printf("error:路径中包含文件, 应该均为文件夹\n");
                exit(0);
            }
            cur_deep ++;      // 又搞了一层文件夹

        }




// 最终文件夹所在inode       已经读入buf_inode，且p_inode已经指向对于对应的inode
    

/// 相当于找文件夹循环出来之后
        if(p_inode->file_type == dir){ // inode 显示为 目录
            // printf("开始在正确的文件夹位置：创建文件了！！！\n");
            int find_index;
            int write_block_index;
            if(judge_need_allo_block(p_inode, &find_index, &write_block_index) == 1){

                // printf("当前文件夹下：有可用的目录项，不必新建block\n");
                // 找到合适的一项在 buf_block[find_index], buf_block要写回 block号为write_block_index的磁盘中

                struct dir_item * p_dir = (struct dir_item *)buf_block;

                p_dir[find_index].valid = 1;                            // 完成目录项
                // printf("即将命名：str[%d] = %s\n", cur_deep, str[cur_deep]);

                memcpy(p_dir[find_index].name, str[cur_deep], strlen(str[cur_deep]));      // 在 /folder 下完成 a.c创建
                
                p_dir[find_index].type = file_;  
                p_dir[find_index].inode_id = inode_index;

                num = write_block_index;                           /// 要写回的block号
                // printf("完成目录项的设置3\n");

            }else{
                // 没有找到空闲block
                // printf("没有空闲的目录项，要新建目录block\n");
                uint32_t new_block_index = find_free_block(p_sp);     // 又找到一个新的block
                
                // 即将分配block号为 new_block_index , 修改超级块 ！！！！！！
                p_sp->free_block_count--;
                set_inode_block_map(-1, new_block_index, p_sp);


                for(int k = 0; k < 6; k++){
                    if(p_inode->block_point[k] == 0){
                        // 将其用上, 目录上新分配一个块 new_block_index
                        p_inode->block_point[k] = new_block_index;       // 修改了p_inode的内容，就要写回去

                        // printf("number_real_hard_inode = %d\n", number_real_hard_inode);
                        disk_write_block(number_real_hard_inode, buf_inode);                      // 在根目录下新分配一个块，根目录也进行了修改，要写回！！
                        disk_write_block(number_real_hard_inode + 1, buf_inode + half_blk_size);
                        
                        // printf("根目录增加了一个块指向\n");
                        // printf("p_inode->block_point[%d] = %d\n", k, p_inode->block_point[k]);
                        

                        // 把new_block_index号block块读入内存
                        disk_read_block(trans_block_num(new_block_index), buf_block);
                        disk_read_block(trans_block_num(new_block_index) + 1, buf_block + half_blk_size);

                        struct dir_item * p_dir = (struct dir_item *)buf_block;
                        p_dir[0].valid = 1;

                        // printf("即将要命名了：str[%d] = %s\n", cur_deep, str[cur_deep]);

                        // printf("cur_deep = %d\n", cur_deep);
                        memcpy(p_dir[0].name, str[cur_deep], strlen(str[cur_deep]));

                        p_dir[0].type = file_;
                        p_dir[0].inode_id = inode_index;

                        num = new_block_index;                     /// 要写回的block号
                        // printf("完成目录项的设置4\n");
                        break;
                    }
                }
            
            }

            // 将num号block写回磁盘
            disk_write_block(trans_block_num(num), buf_block);
            disk_write_block(trans_block_num(num)+1, buf_block + half_blk_size);

        }

        // printf("完成目录项的设置，也指向了inode\n");
        flag = 0;


/*--------------------------------将要被分配的inode读出来！！！（可能和目录所在inode一样，上面先写入）---------------------------------------*/

        char buf_allo_inode[1024];
        disk_read_block(trans_inode_num(inode_i), buf_allo_inode);        // inode在第inode_i号block里
        disk_read_block(trans_inode_num(inode_i) + 1, buf_allo_inode + half_blk_size);

        struct inode * p_allo_inode = (struct inode *)buf_allo_inode;
        p_allo_inode += inode_j;

        // 找到了要改的inode
        p_allo_inode->size = 0;
        p_allo_inode->file_type = file;  
        p_allo_inode->link = 1;
        p_allo_inode->block_point[0] = block_index;     // inode 表明文件第一个块在block_inode号
    

        // printf("完成inode的设置，指向block : %d\n", p_allo_inode->block_point[0]);
        
        disk_write_block(trans_inode_num(inode_i), buf_allo_inode);        // inode在第inode_i号block里
        disk_write_block(trans_inode_num(inode_i) + 1, buf_allo_inode + half_blk_size);
    

        // 开始写回磁盘
        disk_write_block(0, buf_super);
        disk_write_block(1, buf_super + half_blk_size);
    
    }
}

/*
* input: str即源文件路径分割结果，deep即源文件路径分割段数
* 功能：
* 1、返回cp命令的源文件对应的inode号（用于目标文件所在目录项的inode号直接赋值为源文件的inode号）
* 2、源文件的inode对应link数++，并写回磁盘
*/
uint32_t get_inode_information(char str[10][20], int deep){
    int cur_deep = 0;
    disk_read_block(2, buf_inode_src);
    disk_read_block(3, buf_inode_src + half_blk_size);

    struct inode * p_inode = (struct inode *)buf_inode_src;
    p_inode += 2;       // 来到根目录的inode
    // printf("p_inode指向 根目录对应的inode\n");
    struct dir_item * p_dir;
    int flag = 0;
    uint32_t num;
    while(cur_deep < deep){
        flag = 0;
        if(p_inode->file_type == dir){ // inode 显示为 目录

            for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
                if(p_inode->block_point[i] != 0){
                    num = p_inode->block_point[i];      // 是 num号block！！！         真实修改的目录项在block num中！！！修改在buf_block

/*-------------------------------------------  将根目录中对应的block读入到 buf_block中！！！--------------------------------------------*/
                    disk_read_block(trans_block_num(num), buf_block_src);                   
                    disk_read_block(trans_block_num(num)+1, buf_block_src + half_blk_size);
                    // printf("将 %d 号 block 读入buf_block\n",num);
                    // finish 将对应 block完整读入

                    // 读入的是目录     遍历目录找到一个空闲位置，看看有没有
                    struct dir_item * p_dir = (struct dir_item *)buf_block;
                    for(int j = 0; j < 8; j++){

                        if(strcmp(p_dir[j].name, str[cur_deep]) == 0){        // 找到第一层文件夹       去匹配
                            // printf("在创建/folder/b.c的过程中:\n");
                            // printf("找到了folder文件夹对应的inode\n");

                            int index = p_dir[j].inode_id;
                            int index_i = index / 32;
                            int index_j = index % 32;
                        

                            disk_read_block(trans_inode_num(index_i), buf_inode_src);
                            disk_read_block(trans_inode_num(index_i) + 1, buf_inode_src + half_blk_size);

                            // printf("把 %d 号 inode 读到 buf_inode中\n", index);

                            // 相当于 一个轮回，找到目录的Inode
                            p_inode = (struct inode *)buf_inode;
                            p_inode += index_j;

                            // printf("当前p_inode指向 inode号为: %d\n", index);
                            // 真正指向了内存中 文件夹demo的 inode
                            flag = 1;
                            // printf("p_inode重新指向新的一层inode的位置\n");
                            break;
                        }
                    
                    }
                }
                if(flag)
                    break;
            }

            if(flag == 0){
                printf("第 %d 层文件夹未找到\n", cur_deep);
                exit(0);
            }
        }else{
            printf("error:路径中包含文件, 应该均为文件夹\n");
            exit(0);
        }
        cur_deep ++;      // 又搞了一层文件夹

    }

    flag = 0;

    uint32_t inode_src_num;
    if(p_inode->file_type == dir){ // inode 显示为 目录，一会读出的block就是一堆目录项
        // printf("开始在正确的文件夹位置：找源文件了！！！\n");
        for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
            if(p_inode->block_point[i] != 0){
                // printf("进入了根目录的第 %d 个 block\n", i);
                uint32_t num = p_inode->block_point[i];      // 是 num号block

                disk_read_block(trans_block_num(num), buf_block_src);                         // 将 block读出
                disk_read_block(trans_block_num(num)+1, buf_block_src + half_blk_size);
                // finish 将对应 block完整读入

                // 读入的是目录
                p_dir = (struct dir_item *)buf_block;
                
                for(int j = 0; j < 8; j++){
                    if(p_dir[j].valid == 1 && strcmp(p_dir[j].name, str[cur_deep]) == 0){
                        // 找到文件对应的inode了
                        inode_src_num = p_dir[j].inode_id;
                        flag = 1;
                        break;
                    }
                } 
                if(flag){
                    break;
                } 
            }
        }
        
    }
    if(flag){
        disk_read_block(trans_inode_num(inode_src_num/32), buf_inode_src);
        disk_read_block(trans_inode_num(inode_src_num/32)+1, buf_inode_src + half_blk_size);    // 读出来
        struct inode * p = (struct inode *)buf_inode_src;
        p += inode_src_num % 32;
        p->link ++;                        // link增加1,因为被复制
        disk_write_block(trans_inode_num(inode_src_num/32), buf_inode_src);
        disk_write_block(trans_inode_num(inode_src_num/32)+1, buf_inode_src + half_blk_size);    // 写回去


        return inode_src_num;
    }else{
        return 0;
    }

}

/*
* input: 
* str1 、deep1 即源文件路径分割结果
* str2 、deep2 即目标文件路径分割结果
* 功能：实现cp功能
*/
void cp(char str1[10][20], char str2[10][20], int deep1, int deep2){
    
    disk_read_block(0, buf_super);
    disk_read_block(1, buf_super + half_blk_size);
    sp_block *p_sp = (sp_block *)buf_super;                  // 已经将超级块读出来！！！！！！！

    int cur_deep1 = 0;
    int cur_deep2 = 0;
    // printf("0号超级块读入 buf_super\n");

    if(ls_judge(str2, deep2) == 1){                      // 不能重复在某个目录下多次建立a.c
        printf("目标目录下已有相关文件（夹），创建文件失败\n");
        exit(0);
    }
    else
    {

        uint32_t num;

        int flag = 0;


/*------------------------------------------ 将根目录读出来！！！读到buf_inode中 ------------------------------------------------------------------*/                                                                 
        

        disk_read_block(2, buf_inode);
        disk_read_block(3, buf_inode + half_blk_size);
        // printf("把 前32个 inode 读到 buf_inode中\n");

        struct inode * p_inode = (struct inode *)buf_inode;
        p_inode += 2;       // 来到根目录的inode
        // printf("p_inode指向 根目录对应的inode\n");

        int number_real_hard_inode;
        
        while(cur_deep2 < deep2){
            flag = 0;
            if(p_inode->file_type == dir){ // inode 显示为 目录

                for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
                    if(p_inode->block_point[i] != 0){
                        num = p_inode->block_point[i];      // 是 num号block！！！         真实修改的目录项在block num中！！！修改在buf_block

    /*-------------------------------------------  将根目录中对应的block读入到 buf_block中！！！--------------------------------------------*/
                        disk_read_block(trans_block_num(num), buf_block);                   
                        disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                        // printf("将 %d 号 block 读入buf_block\n",num);
                        // finish 将对应 block完整读入

                        // 读入的是目录     遍历目录找到一个空闲位置，看看有没有
                        struct dir_item * p_dir = (struct dir_item *)buf_block;
                        for(int j = 0; j < 8; j++){

                            if(strcmp(p_dir[j].name, str2[cur_deep2]) == 0){        // 找到第一层文件夹       去匹配
                                // printf("在创建/folder/b.c的过程中:\n");
                                // printf("找到了folder文件夹对应的inode\n");

                                int index = p_dir[j].inode_id;
                                int index_i = index / 32;
                                int index_j = index % 32;
                        

                                disk_read_block(trans_inode_num(index_i), buf_inode);
                                disk_read_block(trans_inode_num(index_i) + 1, buf_inode + half_blk_size);

                                // printf("把 %d 号 inode 读到 buf_inode中\n", index);
                                number_real_hard_inode = trans_inode_num(index_i);

                                // 相当于 一个轮回，找到目录的Inode
                                p_inode = (struct inode *)buf_inode;
                                p_inode += index_j;

                                // printf("当前p_inode指向 inode号为: %d\n", index);
                                // 真正指向了内存中 文件夹demo的 inode
                                flag = 1;
                                // printf("p_inode重新指向新的一层inode的位置\n");
                                break;
                            }
                        
                        }
                    }
                    if(flag)
                        break;
                }

                if(flag == 0){
                    printf("第 %d 层文件夹未找到\n", cur_deep2);
                    exit(0);
                }
            }
            cur_deep2 ++;      // 又搞了一层文件夹

        }

// 最终文件夹所在inode       已经读入buf_inode，且p_inode已经指向对于对应的inode

/// 找文件夹循环出来之后    
        uint32_t src_inode = get_inode_information(str1, deep1);
        
        // if(src_inode == 0){
        //     printf("源文件不存在\n");
        //     exit(0);
        // }

        if(p_inode->file_type == dir){ // inode 显示为 目录
            // printf("开始在正确的文件夹位置：复制 文件了 只是设个目录项而已！！！\n");
            int find_index;
            int write_block_index;
            if(judge_need_allo_block(p_inode, &find_index, &write_block_index) == 1){

                // printf("当前文件夹下：有可用的目录项，不必新建block\n");
                // 找到合适的一项在 buf_block[find_index], buf_block要写回 block号为write_block_index的磁盘中

                struct dir_item * p_dir = (struct dir_item *)buf_block;

                p_dir[find_index].valid = 1;                            // 完成目录项
                memcpy(p_dir[find_index].name, str2[cur_deep2], strlen(str2[cur_deep2]));      // 在 /folder 下完成 a.c创建
                p_dir[find_index].type = file_;   //////////////////////////////////////////////////////////////////////////////////////////////////
                p_dir[find_index].inode_id = src_inode;

                num = write_block_index;                           /// 要写回的block号
                // printf("完成目录项的设置3\n");

            }else{
                // 没有找到空闲block
                uint32_t new_block_index = find_free_block(p_sp);     // 又找到一个新的block
                
                // 即将分配block号为 new_block_index , 修改超级块 ！！！！！！
                p_sp->free_block_count--;
                set_inode_block_map(-1, new_block_index, p_sp);


                for(int k = 0; k < 6; k++){
                    if(p_inode->block_point[k] == 0){
                        // 将其用上, 目录上新分配一个块 new_block_index
                        p_inode->block_point[k] = new_block_index;       // 修改了p_inode的内容，就要写回去

                        disk_write_block(number_real_hard_inode, buf_inode);                      // 在根目录下新分配一个块，根目录也进行了修改，要写回！！
                        disk_write_block(number_real_hard_inode + 1, buf_inode + half_blk_size);

                        
                        // printf("根目录增加了一个块指向\n");
                        // printf("p_inode->block_point[%d] = %d\n", k, p_inode->block_point[k]);
                        

                        // 把new_block_index号block块读入内存
                        disk_read_block(trans_block_num(new_block_index), buf_block);
                        disk_read_block(trans_block_num(new_block_index) + 1, buf_block + half_blk_size);

                        struct dir_item * p_dir = (struct dir_item *)buf_block;
                        p_dir[0].valid = 1;
                        memcpy(p_dir[0].name, str2[cur_deep2], strlen(str2[cur_deep2]));
                        p_dir[0].type = file_;
                        p_dir[0].inode_id = src_inode;

                        num = new_block_index;                     /// 要写回的block号
                        // printf("完成目录项的设置4\n");
                        break;
                    }
                }
            
            }

            // 将num号block写回磁盘
            disk_write_block(trans_block_num(num), buf_block);
            disk_write_block(trans_block_num(num)+1, buf_block + half_blk_size);

        }

        // printf("完成目录项的设置，也指向了inode\n");
        flag = 0;


/*--------------------------------将要被分配的inode读出来！！！（可能和目录所在inode一样，上面先写入）---------------------------------------*/
        

        // 开始写回磁盘
        disk_write_block(0, buf_super);
        disk_write_block(1, buf_super + half_blk_size);
    
    }
}

/*
* 完成根目录下内容的输出
*/
void ls_root(){
    disk_read_block(2, buf_inode);                           // 读出根目录inode
    disk_read_block(3, buf_inode + half_blk_size);

    struct inode * p_inode = (struct inode *)buf_inode;
    p_inode += 2;       // 来到根目录的inode

    if(p_inode->file_type == dir){ // inode 显示为 目录，一会读出的block就是一堆目录项

        for(int i = 0; i < 6; i++){               // 该文件对应的第i个block
            if(p_inode->block_point[i] != 0){
                // printf("进入了根目录的第 %d 个 block\n", i);
                uint32_t num = p_inode->block_point[i];      // 是 num号block

                disk_read_block(trans_block_num(num), buf_block);                         // 将 block读出
                disk_read_block(trans_block_num(num)+1, buf_block + half_blk_size);
                // finish 将对应 block完整读入

                // 读入的是目录
                struct dir_item * p_dir = (struct dir_item *)buf_block;
                
                for(int j = 0; j < 8; j++){
                    if(p_dir[j].valid == 1){
                        printf("%s\n", p_dir[j].name);
                    }
                }  
            }
        }
        
    }
}

// 要求文件路径中不包含空格

char str1[10][20];
char str2[10][20];

// void print(int num, char str[10][20]){
//     printf("--------------print-----\n");
//     int i;
//     for(i = 0; i < num; i++){
//         printf("str[%d]: ", i);
//         puts(str[i]);
//     }
//     printf("str[%d] : ", i);
//     puts(str[i]);
// }


// 函数功能：实现将 a/b/c 分成 a b c 存入3个char[], 返回数目 2
/*
* input: 
* string 即 输入的命令后面的路径部分  (eg. ls /home/a.c 则string : home/a.c)
* str 即 string 切割后的存放位置
* off 用于返回遇到空格的位置（以便cp 命令将string 后移off位 ；再调用trans_str函数 进行目标文件路径的分割）
*/
int trans_str(char * string, char str[10][20], int * off){

    int len = strlen(string);    // str不包含'\0'的长度
    // printf("----------------trans_str---------------\n");
    // for(int q = 0; q < len; q++){
    //     printf("%c", string[q]);
    // }
    // printf("\n");
    
    int k = 0;
    *off = -1;             

    int i = 0, j = 0;
    while(k < len){
        if(string[k] != '/' && string[k] != ' '){
            str[i][j++] = string[k];
        }else if(string[k] == '/'){
            str[i][j++] = '\0';
            i++;
            j = 0;
        }else if(string[k] == ' '){
            // 当前字符结束了
            *off = k;
            break;
        }else{
            ;
        }
        k++;
    }
    str[i][j] = '\0';
    return i; // i == 2  即c之前有2层，实际有效是[0][1][2]3行
}
/*
* eg: option : string 如下
* 1: ls /home => home
* 2: touch /test/a.c => test  a.c
* 3: mkdir /home => home
* 4: cp /test.c /home/tmp.c   => test.c    home tmp.c
*/
void devide_do(char *string, int option){       // 传入 无前/的字符串，以及选项
    // printf("----------------devide_do---------------\n");
    // for(int q = 0; q < strlen(string); q++){
    //     printf("%c", string[q]);
    // }
    // printf("\n");
    // printf("option = %d\n", option);

    int i,j;
    int k = 0;
    int len = strlen(string);
    int num;
    int num2;
    int off;
    switch(option){
        case 1:
            num = trans_str(string, str1, &off);
            // 调用ls_ok
            // print(num, str1);
            // printf("------------------------------调用ls_ok\n");          // 查看任意路径文件夹下的文件 可以吗？
            // printf("num = %d\n\n", num);
            ls_ok(str1, num+1);
            // ls_root();
            break;
        case 2:             // 能在任意已有路径下建立文件？？？
            num = trans_str(string, str1, &off);
            // 调用touch
            // print(num, str1);
            // printf("num = %d\n", num);
            // printf("调用create_file在根目录下\n");                    // 在已用任意文件夹路径下  建立文件可以
            // printf("----------------------------------在已有目录下建立文件\n");
            create_file_ok(str1, num);                     // 真的可以在根目录下建立文件  可以 touch /1.c
            // create_file(str1);
            break;
        case 3:
            num = trans_str(string, str1, &off);
            // 调用mkdir
            // print(num, str1);
            // printf("调用create_folder在根目录下\n");
            create_folder_ok(str1, num);                                     // 在任意目录下建立文件夹      可以 mkdir /test
            break;      
        case 4:
            num = trans_str(string, str1, &off);
            
            // if(string[k] == ' '){
            //     printf("分对了，第一部分\n");
            // }
            num2 = trans_str(string+off+2, str2, &off);
            // 调cp
            // printf("第一个路径如下:\n");
            // print(num, str1);
            // printf("\n第二个路径如下:\n");
            // print(num2, str2);
            // printf("------------------------------------------------调用cp\n");
            cp(str1, str2, num, num2);
            break; 
        default:
            ;

    }
}

// ls /home

// 完成将：名字划分，类别划分          // str是输入的所有内容
int solve(char *str, int len){
    // printf("------------------solve---------------\n");
    // for(int q = 0; q < len; q++){
        // printf("%c", str[q]);
    // }
    // printf("\n\n");
    // printf("len = %d\n", len);

    int k = 0;
    if(len < 2)
        return 0;
    if(str[0] == 'l' && str[1] == 's' && str[2] == '\0' || str[0] == 'l' && str[1] == 's' && str[2] == ' ' && str[3] == '/' && str[4] == '\0'){
        ls_ok(str1, 0);     // 关键：第二个参数为0 即代表根目录
    }else if(str[0] == 'l' && str[1] == 's'){
          // 显示某个目录下内容     
        devide_do(str+4, 1);
            
    }else if(str[0] == 't' && str[1] == 'o' && str[2] == 'u' && str[3] == 'c' && str[4] == 'h'){
        devide_do(str+7, 2);
     
    }else if(str[0] == 'm' && str[1] == 'k' && str[2] == 'd' && str[3] == 'i' && str[4] == 'r'){
        devide_do(str+ 7, 3);
    }else if(str[0] == 'c' && str[1] == 'p'){
        devide_do(str+4, 4);
    }else if(str[0] == 's' && str[1] == 'h' && str[2] == 'u' && str[3] == 't' && str[4] == 'd' && str[5] == 'o' && str[6] == 'w' && str[7] == 'n'){
        // printf("调用close_disk\n");
        return 0;
    }else{
        printf("no define option\n");
        return 0;

    }
    return 1;

}


int main(){
    int flag = 1;
    open_disk();

    disk_read_block(0, buf_super);                     // 读出超级块
    disk_read_block(1, buf_super + half_blk_size);

    printf("读出了超级块\n");
    sp_block * sp_tr = (sp_block *)buf_super;     // 设置访问超级块的指针

    if(sp_tr != NULL){
        if(sp_tr->magic_num == 0){    // 未创建系统
            printf("开始init\n");
            init(sp_tr);       // 已经写回磁盘块了
            printf("执行完Init\n");
        }else if(sp_tr->magic_num != 0xdec0de){
            printf(" 开机 error\n");
            exit(0);
        }else{
            ;
        }
        do{        // 执行命令
            char str[121];
            printf(">> ");
            
            int p = 0;
            while((str[p]=getchar())!='\n') p++;
            str[p]='\0';
            // printf("直接命令如下:\n");
            // for(int k = 0; k < p; k++){
                // printf("%c", str[k]);
            // }
            // printf("\n进入solve----------------------\n");
            flag = solve(str, p);
        }while(flag);

        close_disk();

    }


    return 0;
}