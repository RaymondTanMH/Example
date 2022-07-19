/******************************************************************************/
/*!
 \file ObjectAllocator.cpp
 \author name: Afrina Saragih
 \par    email: a.reimansaragih@digipen.edu
 \par    DigiPen login:a.reimansaragih
 \par    Course: CS280
 \par    Assignment #1
 \date   27/05/2022
 \brief
    This file contains the implementation of the following functions for
    the ObjectAllocator class:

    Public functions:
    ObjectAllocator (constructor)
    ~ObjectAllocator
    Allocate
    Free
    DumpMemoryInUse
    ValidatePages
    FreeEmptyPages
    SetDebugState
    GetFreeList
    GetPageList
    GetConfig
    GetStats

    Private functions:
    newPage
    setPattern

   Hours spent on this assignment: 60

   Specific portions that gave you the most trouble: FreeEmptyPages()

*/
/******************************************************************************/
#include <cstring>
#include "ObjectAllocator.h"

const size_t PT_SIZE = sizeof(void*);
const size_t PAD_BLOCKS = 2;
/******************************************************************************/
/*!

 \brief
   ObjectAllocator constructor.

 \param ObjectSize
   Size of a object.

 \param config
   Configuration for the ObjectAllocator class.

*/
/******************************************************************************/
ObjectAllocator::ObjectAllocator(size_t ObjectSize, const OAConfig& config):
    PageList_{nullptr}, FreeList_{nullptr}, config_{config}, 
    stats_{OAStats()}, blockSz_{0}
{
    stats_.ObjectSize_ = ObjectSize;
    if(config_.Alignment_>1){
        config_.LeftAlignSize_= static_cast<unsigned>(config_.Alignment_
            -((PT_SIZE +config_.HBlockInfo_.size_
            +config_.PadBytes_)%config_.Alignment_));
            
        config_.InterAlignSize_= static_cast<unsigned>(config_.Alignment_
            -((stats_.ObjectSize_+config_.PadBytes_*PAD_BLOCKS
            +config_.HBlockInfo_.size_)%config_.Alignment_));
        
    }
    blockSz_ = config_.HBlockInfo_.size_ + stats_.ObjectSize_
            + config_.PadBytes_ * PAD_BLOCKS + config_.InterAlignSize_;
    stats_.PageSize_ = PT_SIZE + config_.LeftAlignSize_
        +blockSz_*config_.ObjectsPerPage_-config_.InterAlignSize_;
        
    if(!config_.UseCPPMemManager_){
        newPage();
    }
    SetDebugState(config_.DebugOn_);
}
/******************************************************************************/
/*!

 \brief
   Destructor of ObjectAllocator class.

*/
/******************************************************************************/
ObjectAllocator::~ObjectAllocator(){
    GenericObject *tempPtr =  PageList_, *prevPtr = nullptr;
    while(tempPtr){

        prevPtr=tempPtr;
        tempPtr = tempPtr->Next;
        delete[] reinterpret_cast<char*>(prevPtr);
    }

}
/******************************************************************************/
/*!

 \brief
   This function provides the client with memory of requested size

 \param label
   A label for external header block

 \return
   Return a pointer to the allocated memory

*/
/******************************************************************************/
void* ObjectAllocator::Allocate(const char *label){
    
    
    //use cpp new
    if(config_.UseCPPMemManager_){
        char * temp = nullptr;
        try{
            temp = new char[stats_.ObjectSize_];
        }
        catch(std::bad_alloc const&){
            throw OAException(OAException::E_NO_MEMORY, 
                "Exception: E_NO_MEMORY");
        }
        stats_.Allocations_++;
        stats_.MostObjects_++;
        return temp;
    }
    
   
    if(stats_.FreeObjects_==0){
        if(stats_.PagesInUse_ < config_.MaxPages_){
            newPage();
        }
        else{
            throw OAException(OAException::E_NO_PAGES, 
                "Exception: E_NO_PAGES");
        }
    }
    GenericObject* memBlock;
    memBlock = FreeList_;
    FreeList_ = FreeList_->Next;    
    char* tempMemset = reinterpret_cast<char*>(memBlock); 
   

    setPattern(tempMemset, stats_.ObjectSize_, 
        ObjectAllocator::ALLOCATED_PATTERN);
    stats_.Allocations_++;

    switch (config_.HBlockInfo_.type_){
        case OAConfig::hbNone:
            break;
        case OAConfig::hbBasic:{
            tempMemset -= (config_.PadBytes_ + config_.HBlockInfo_.size_);
            memcpy(reinterpret_cast<unsigned*>(tempMemset),
                &stats_.Allocations_, sizeof(unsigned));

            memset(tempMemset + 4, sizeof(char), 1);
            break;
        }
        case OAConfig::hbExtended:{
            tempMemset -= (config_.PadBytes_ + config_.HBlockInfo_.size_);
            (*(reinterpret_cast<unsigned short*>(tempMemset 
                + config_.HBlockInfo_.additional_)))++;
                
            tempMemset += config_.HBlockInfo_.additional_ 
                + sizeof(unsigned short);
            
            memcpy(reinterpret_cast<unsigned*>(tempMemset), 
                &stats_.Allocations_, sizeof(unsigned));
            
            tempMemset += sizeof(unsigned);
            //set allocation flag
            memset(tempMemset, sizeof(char), 1);
          break;
        }
        case OAConfig::hbExternal:{
            tempMemset -= (config_.PadBytes_ + config_.HBlockInfo_.size_);
            GenericObject* tempMem = 
                reinterpret_cast<GenericObject*>(tempMemset);
                
            tempMem->Next = 
                reinterpret_cast<GenericObject*>(new MemBlockInfo());
                
            //set allocation num
            reinterpret_cast<MemBlockInfo*>(tempMem->Next)->alloc_num = 
                stats_.Allocations_;
            
            reinterpret_cast<MemBlockInfo*>(tempMem->Next)->in_use = true;
            if(label){
                reinterpret_cast<MemBlockInfo*>(tempMem->Next)->label = 
                    new char[strlen(label) + 1]; 
                strcpy(reinterpret_cast<MemBlockInfo*>(tempMem->Next)->label,
                    label);
            }
            else
                reinterpret_cast<MemBlockInfo*>(tempMem->Next)->label = nullptr;
            break;
        }
        default:
            break;
    }
    
    //update stats
    
    stats_.ObjectsInUse_++;
    stats_.FreeObjects_--;
    if(stats_.MostObjects_<stats_.ObjectsInUse_){
        stats_.MostObjects_=stats_.ObjectsInUse_;
    }
    return memBlock;
}
/******************************************************************************/
/*!

 \brief
   This function frees the specific object

 \param Object
   Object to be freed

*/
/******************************************************************************/
void ObjectAllocator::Free(void *Object){
    GenericObject* freePtr = FreeList_ , 
        *objPtr =reinterpret_cast<GenericObject*>(Object), *pagePtr = PageList_;
    bool foundInPage = false;
    if(config_.UseCPPMemManager_){
        delete[] reinterpret_cast<char*>(Object);
        stats_.Deallocations_++;
        stats_.ObjectsInUse_--;
        stats_.FreeObjects_++;
        return;
    }
    if(debugState){
        while(freePtr){
            if(objPtr  == freePtr){
       
                throw OAException(OAException::E_MULTIPLE_FREE,  
                    "Object was already  freed!");
            }
            freePtr = freePtr->Next;
        }

        while(pagePtr){
            if(objPtr == pagePtr){
                throw OAException(OAException::E_BAD_BOUNDARY, 
                    "Object not on Block Boundary");
            }

            if((objPtr > pagePtr) && (reinterpret_cast<char*>(objPtr) < 
                (reinterpret_cast<char*>(pagePtr)+stats_.PageSize_))){
                    
                foundInPage  = true;
                
                size_t offset=reinterpret_cast<char*>(objPtr)-PT_SIZE
                    -config_.LeftAlignSize_-config_.PadBytes_
                    -config_.HBlockInfo_.size_-reinterpret_cast<char*>(pagePtr);
                if(offset%blockSz_){
           
                    throw OAException(OAException::E_BAD_BOUNDARY, 
                        "Object not on Block Boundary");
                }
        
                break;
            }
            
            pagePtr = pagePtr->Next;
        }
        if(!foundInPage){
       
            throw OAException(OAException::E_BAD_BOUNDARY, 
                            "Object not on Block Boundary");
        }
        if(config_.PadBytes_){
            unsigned char *tempItr = reinterpret_cast<unsigned char*>(objPtr);
            if(*(tempItr-config_.PadBytes_)!=PAD_PATTERN){
                
                throw OAException(OAException::E_CORRUPTED_BLOCK, 
                    "Block corrupted");
            }
            if(*(tempItr+stats_.ObjectSize_)!=PAD_PATTERN){
                
                throw OAException(OAException::E_CORRUPTED_BLOCK, 
                    "Block corrupted");
            }
        }
        
    }
    setPattern(reinterpret_cast<char*>(objPtr), stats_.ObjectSize_, 
        ObjectAllocator::FREED_PATTERN);
        
    char* tempMemset = reinterpret_cast<char*>(objPtr)-config_.PadBytes_
        -config_.HBlockInfo_.size_;
    switch (config_.HBlockInfo_.type_){
        case OAConfig::hbNone:
            break;
        case OAConfig::hbBasic:{
            memset(reinterpret_cast<unsigned*>(tempMemset),
                0, sizeof(unsigned));
            tempMemset += sizeof(unsigned);
            memset(tempMemset, 0, sizeof(char));
            break;
        }
        case OAConfig::hbExtended:{
            tempMemset+=config_.HBlockInfo_.additional_+sizeof(unsigned short);
            memset(reinterpret_cast<unsigned*>(tempMemset), 
                0, sizeof(unsigned));
            tempMemset+=sizeof(unsigned);
            memset(tempMemset , 0, sizeof(char));
            break;
        }
        case OAConfig::hbExternal:{
            MemBlockInfo* tempMem = 
                reinterpret_cast<MemBlockInfo*>
                    (reinterpret_cast<GenericObject*>(tempMemset)->Next);
            tempMem->in_use = false;
            
            delete[] tempMem->label;
            tempMem->label = nullptr;
            delete reinterpret_cast<GenericObject*>(tempMemset)->Next;
            reinterpret_cast<GenericObject*>(tempMemset)->Next = nullptr;
            break;
        }
    }
    objPtr->Next=FreeList_;
    FreeList_=objPtr;
    stats_.FreeObjects_++;
    stats_.ObjectsInUse_--;
    stats_.Deallocations_++;
    
}
/******************************************************************************/
/*!

 \brief
   This function dumps the memory blocks currently in use

 \param fn
    callback function to call with memory block info

 \return
    number of memory blocks in use

*/
/******************************************************************************/
unsigned ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK fn) const{
    GenericObject *tempPtr = PageList_,  *tempFree = nullptr;
    char *tempItr = nullptr;
    bool isFree = false;
    unsigned count = 0;
    while(tempPtr){
        tempItr = reinterpret_cast<char*>(tempPtr) + PT_SIZE 
            + config_.HBlockInfo_.size_+config_.PadBytes_;
            
        for(unsigned i = 0; i<config_.ObjectsPerPage_;i++){
            
            tempFree = FreeList_;
            while(tempFree){
                if(tempItr==reinterpret_cast<char*>(tempFree)){
                    isFree=true;
                    break;
                }
                tempFree = tempFree->Next;
            }
            if(!isFree){
                fn(tempItr, stats_.ObjectSize_);
                count++;
            }
            isFree=false;
            tempItr+=blockSz_;
        }
        tempPtr = tempPtr->Next;
    }
    return count;
}
/******************************************************************************/
/*!

 \brief
    Validates Pages to check for corruption

 \param fn
    callback function to call when corrupted block found

 \return
    return number of corrupted blocks

*/
/******************************************************************************/
unsigned ObjectAllocator::ValidatePages(VALIDATECALLBACK fn) const{
    unsigned count=0;
    GenericObject *tempPtr = PageList_;
    unsigned char *tempItr = nullptr;
    if((debugState==false)||(config_.PadBytes_==0)){
        return 0;
    }
    while(tempPtr){
        tempItr = reinterpret_cast<unsigned char *>(tempPtr) + PT_SIZE 
            + config_.HBlockInfo_.size_+config_.PadBytes_;
        for(unsigned i =0;i<config_.ObjectsPerPage_;i++){
            
            for(unsigned j=0;j<config_.PadBytes_;j++){
                
                if((*(tempItr-config_.PadBytes_)!=PAD_PATTERN)|| 
                    (*(tempItr+stats_.ObjectSize_)!=PAD_PATTERN)){
                    fn(tempItr, stats_.ObjectSize_);
                    count++;
                    break;
                }
               
            }
            tempItr+=blockSz_;
        }
        tempPtr=tempPtr->Next;
    }

    return count;
}
/******************************************************************************/
/*!

 \brief
   This function frees pages that are not in use


 \return
   number of pages freed

*/
/******************************************************************************/
unsigned ObjectAllocator::FreeEmptyPages(){
    GenericObject *tempPtr = PageList_,*nextptr=nullptr, *freeItr=nullptr, 
        *prevPtr =tempPtr, *prevFreeItr=nullptr;
        
    char *objPtr =nullptr;
    unsigned countFreePages = 0, freeObjectsInPage=0;
    if(config_.UseCPPMemManager_){
        return countFreePages;
    }
    if(PageList_==nullptr){
        return countFreePages;
    }
    
    while(tempPtr){
        
        freeObjectsInPage=0;
        objPtr=reinterpret_cast<char*>(tempPtr) + PT_SIZE 
            +config_.LeftAlignSize_+ config_.HBlockInfo_.size_
            +config_.PadBytes_;
                
        for(unsigned i=0;i<config_.ObjectsPerPage_;i++){
            
            freeItr = FreeList_;
            while(freeItr){
                if(freeItr==reinterpret_cast<GenericObject*>(objPtr)){
                    
                    freeObjectsInPage++;
                    
                    break;
                }
             
                freeItr = freeItr->Next;
            }
            objPtr+=blockSz_;
        }
        nextptr=tempPtr->Next;
        
        if(freeObjectsInPage==config_.ObjectsPerPage_){
            
            objPtr=reinterpret_cast<char*>(tempPtr)+PT_SIZE
                +config_.HBlockInfo_.size_+config_.LeftAlignSize_
                    +config_.PadBytes_;
                
            for(unsigned i=0;i<config_.ObjectsPerPage_;i++){
                freeItr=FreeList_;
                prevFreeItr = freeItr;
                while(freeItr){
                    if(reinterpret_cast<char*>(freeItr)==objPtr){
                        if(freeItr==FreeList_){
                            FreeList_=FreeList_->Next;
                        }
                        else{
                            prevFreeItr->Next=freeItr->Next;
                            
                        }
                        break;
                    }
                    prevFreeItr=freeItr;
                    freeItr= freeItr->Next;
                
                }
                objPtr+=blockSz_;
            }

            GenericObject* deletePage = tempPtr;
            if(tempPtr == PageList_){
                PageList_=PageList_->Next;
                tempPtr = PageList_;
                prevPtr = tempPtr;
                
            } 
            else{
                prevPtr->Next=tempPtr->Next;
                tempPtr=prevPtr->Next;
            }
            delete[] deletePage;

            countFreePages++;
            stats_.PagesInUse_--;
            stats_.FreeObjects_-=config_.ObjectsPerPage_;
        }
        else{
           prevPtr = tempPtr;
           tempPtr = nextptr;
           
        }     
        
    }
    return countFreePages;
}
/******************************************************************************/
/*!

 \brief
   This function sets memory signatures if debug is true

 \param temp
   address to set memory

 \param size
   length of memory to be set
   
 \param pattern
   pattern to set

*/
/******************************************************************************/
void ObjectAllocator::setPattern( char* temp, size_t size, 
    unsigned char pattern){
  
    if(debugState==true){
        memset(temp, pattern, size);
    }
}
/******************************************************************************/
/*!

 \brief
   creates new page


*/
/******************************************************************************/
void ObjectAllocator::newPage(){
    char* newPg = nullptr, *tempPtr = nullptr, 
        *prevPtr=reinterpret_cast<char*>(FreeList_);
    try{
        newPg = new char[stats_.PageSize_];
    }
    catch(std::bad_alloc const&){
        throw OAException(OAException::E_NO_MEMORY, "newPage(): out of memory");
    }
    
    
    reinterpret_cast<GenericObject*>(newPg) -> Next = PageList_;
    PageList_ = reinterpret_cast<GenericObject*>(newPg);
    tempPtr = newPg+PT_SIZE;
    setPattern(tempPtr, config_.LeftAlignSize_, ALIGN_PATTERN);
    tempPtr += config_.LeftAlignSize_ + config_.HBlockInfo_.size_
        +config_.PadBytes_;
    
    for(unsigned i=0;i<config_.ObjectsPerPage_;i++){
                   
        memset(tempPtr-config_.PadBytes_-config_.HBlockInfo_.size_, 0, 
            config_.HBlockInfo_.size_);
        
        setPattern(tempPtr - config_.PadBytes_, config_.PadBytes_, 
            PAD_PATTERN);
        setPattern(tempPtr, stats_.ObjectSize_, UNALLOCATED_PATTERN);
        setPattern(tempPtr + stats_.ObjectSize_, 
            config_.PadBytes_, PAD_PATTERN);
        
        if(i<(config_.ObjectsPerPage_-1)){
            setPattern(tempPtr + stats_.ObjectSize_ + config_.PadBytes_ , 
                config_.InterAlignSize_, ObjectAllocator::ALIGN_PATTERN);
        }
        reinterpret_cast<GenericObject*>(tempPtr)->Next = 
            reinterpret_cast<GenericObject*>(prevPtr);

        prevPtr=tempPtr;
        tempPtr += blockSz_; 
    }
    
    FreeList_=reinterpret_cast<GenericObject*>(prevPtr);
    stats_.FreeObjects_+=config_.ObjectsPerPage_;
    stats_.PagesInUse_++;
    
}
/******************************************************************************/
/*!

 \brief
   This function sets the debug state

 \param State
   state to set debug to


*/
/******************************************************************************/
void ObjectAllocator::SetDebugState(bool State){
    debugState = State;
}	

/******************************************************************************/
/*!

 \brief
   This function returns the free list

 \return
   Linked List of free objects

*/
/******************************************************************************/
const void* ObjectAllocator::GetFreeList() const{
    return FreeList_;
}	
/******************************************************************************/
/*!

 \brief
   This function returns the page list
 \return
   Linked List of pages

*/
/******************************************************************************/
const void* ObjectAllocator::GetPageList() const{
    return PageList_;
}	
/******************************************************************************/
/*!

 \brief
   This function returns the OAConfig

 \return
   Object allocator configuration

*/
/******************************************************************************/
OAConfig ObjectAllocator::GetConfig() const{
    return config_;
}	
/******************************************************************************/
/*!

 \brief
   This function returns the OAStats

 \return
   Object allocator stats

*/
/******************************************************************************/
OAStats ObjectAllocator::GetStats() const{
    return stats_;
}	
