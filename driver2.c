#include <stdio.h>
#include<math.h>
#include "configuration.h"
#include "cache.h"
#include <stdlib.h>
#include "dataTypes.h"
#include "pageTable.h"
#include "frameTable.h"
#include "segmentTable.h"
#include "pcb.h"
#include "tlb.h"
//#include "utility.h"
#define NUM_LINES_BEFORE_CONTEXT_SWITCH 200

int readAddr(FILE *fp)
{
    int addr;
    if(fscanf(fp,"%x",&addr)!=EOF)
        return addr;
    else{
       // printf("eof\n");
        return -1;
    }

}
int4 readSegNum(FILE *fp, char *write)
{
    int4 val;
   int x;
    if(fscanf(fp,"%x %c",&x,write)!=EOF){
        val.value = x;
        return val;
    }
    else{
       // printf("eof\n");
        val.value=-1;
        return val;
    }

}
FILE *outputFile;

PCB pcbArr[30];
segmentTableInfo* GDTptr;

//TLB objects of L1 and L2
TLBL1 tlbL1Obj;
TLBL2 tlbL2Obj;

//Cache objects L1 and L2
CacheL1 cacheL1obj[2];   //L1 instruction and data cache
CacheL2 cacheL2obj;

FrameTable frameTable;

long long current_time = 0;

int main()
{
    int n;      //Number of processes  
    FILE* input = fopen("standard_input.txt", "r");

    fscanf(input, "%d", &n);

    int error;

    char SegAddrInputFileName[n][100];
    char LinearAddrInputFileName[n][100];
    for(int i = 0; i < n; ++i)
    {
        fscanf(input, "%s", LinearAddrInputFileName[i]); 
    }
    for(int i = 0; i < n; ++i)
    {
        fscanf(input, "%s", SegAddrInputFileName[i]);
    }

    fclose(input);
   
    
    //Flushing the TLBs initially
    TLBL1Flush();
    TLBL2Flush();


    //Initializing Cache
    initL1Cache();
    initL2Cache();


    //Initialize Frame Table
    initFrameTable();


    //Global descriptor Table initialize
    GDTptr=initGDTable();

    printf("GDT Initialised");

    //Initializing PCBs of all Processes
    //Initialize segment Table for each Process
    for(int i = 0; i < n; ++i)
    {
        //Initializes PCB
        initPCB(i, LinearAddrInputFileName[i], SegAddrInputFileName[i]);
        printf("\n");
        printf("Checking if GDT segment descriptor is present: %d",GDTptr->segmentTableObj->entries[i].present);
        printf("\n");
        //initPCB(&pcbArr[i]);

    }

    //Opening outputFile
    outputFile = fopen(OUTPUT_FILE_NAME, "w");
   // fclose(outputFile);

    int numProcessAlive = n;
while(numProcessAlive)
{
    for(int i = 0; i < n; ++i)
    {
        
        //Changes the status of the process
        if(getState(pcbArr[i]) == TERMINATED)
        {
            ++current_time;
            continue;
        }
    
        setState(&pcbArr[i], RUNNING);
        ++current_time;
        fprintf(outputFile, "\n\n\nDriver: Process-%d running in the processor.\n", i);
    


        for(int j = 0; j < NUM_LINES_BEFORE_CONTEXT_SWITCH; ++j)
        {
            //time initialized to zero after every context switch
            long time = 0;

            //Reads the Memory reference from input file
            int inputAddr = readAddr(pcbArr[i].LinearAddrInputFile);
        
            char write;
            int4 segNum = readSegNum(pcbArr[i].SegNumAddrFile, &write);
            
            unsigned int readWrite = 0;             //All accesses read to avoid errors for now

            if(inputAddr==-1)
            {
                setState(&pcbArr[i],TERMINATED );
                fprintf(outputFile, "Process %d is terminated\n\n", i);
                --numProcessAlive;
                break;
            }
            printf("%x %d %c\n",inputAddr,segNum.value,write);

            //First we find the correct mapping of Logical Address to Physical Address

            // Split the inputAddr to find the requested pageNum.
            int requestedPageOffset = inputAddr & 1023; // (pow(2, 10) - 1), Since page size is 2**10B.
            int requestedPageNum = inputAddr >> 10; // Since we need to discard the least significant 10b.

            printf("%x  %x\n",requestedPageNum,requestedPageOffset);
            // Based on the MSB of the segNum, decide which cache to hit (data or instr).
            bool dataCache = (segNum.value == 0) ? true : false;


            // Search this pageNum in both levels of TLB.
           fprintf(outputFile, "Driver: Memory access for %x requested by process-%d\n", inputAddr, i);
            int frameNum = TLBL1Search(requestedPageNum,&error);

            time += L1_TLB_SEARCH_TIME;
            fprintf(outputFile, "Driver: Searched through L1 TLB. Time cost: %d Frame#%d\n", L1_TLB_SEARCH_TIME,frameNum);

            if (frameNum < 0)
            {
                time += L2_TLB_SEARCH_TIME;
                fprintf(outputFile, "Driver: Did not find required data in L1 TLB. Searched through L2 TLB. Time cost: %d\n", L2_TLB_SEARCH_TIME);
                frameNum = TLBL2Search(requestedPageNum,&error);
                if (frameNum < 0) // If the data is not present in TLBL2 also.
                {
                    // Get the data from MM.
                    unsigned int* pageFaultPageNumber=calloc(1, sizeof(unsigned int));
                    unsigned int* level=calloc(1, sizeof(unsigned int));
                    // if(dataCache)        //if memory reference is data
                    // {   
                    //     frameNum = searchPageTable(PCB[i].segTableObj->entries[segNum], ptrToPageFaultTable, 
                    //                                 inputAddr, readWrite, pageFaultPageNumber, level);
                    // }
                    // else{               //if memory reference is code
                    //     frameNum = searchPageTable(GDT[PCB[i].GDTindex]->pageTableObj, ptrToPageFaultTable, 
                    //                             inputAddr, readWrite, pageFaultPageNumber, level);    
                    // }
                    pageTable* pagetable;
                    pageTable** ptrToPageFaultPageTable = malloc(sizeof(pageTable *));

                    pagetable = searchSegmentTable(i, segNum);

                    frameNum = searchPageTable(pagetable, ptrToPageFaultPageTable, inputAddr,readWrite,pageFaultPageNumber,level);


                    if(frameNum == -2)
                    {
                        //Invalid Address
                        fprintf(outputFile, "Driver: InvalidAddress\n");
                        current_time += time;
                        pcbArr[i].runTime += time;
                        ++current_time;
                        continue;
                    }



                    if(frameNum == -1)
                    {

                        printf("After returing to driver searchPageTable: level = %d, pageFaultPageNumber = %d\n", *level, *pageFaultPageNumber);
        
                        allocateFrame(i,segNum.value,*ptrToPageFaultPageTable, *pageFaultPageNumber, *level);

                        fseek(pcbArr[i].LinearAddrInputFile,-9*sizeof(char),SEEK_CUR);
                        fseek(pcbArr[i].SegNumAddrFile,-4*sizeof(char),SEEK_CUR);

                        current_time += time;
                        pcbArr[i].runTime += time;
                        ++current_time;
                        break;
                     }
                    
        
                    fprintf(outputFile, "Driver: Searched through PageTable, found the frameNumber for the required memory reference\n");

                    time += MM_SEARCH_TIME;
                    fprintf(outputFile, "Driver: Did not find required Addr mapping in L2 TLB. Searched through MM PageTable. Time cost: %d\n", MM_SEARCH_TIME);

                    TLBL2Update(requestedPageNum, frameNum);
                    time += L2_TLB_UPDATE_TIME;
                    fprintf(outputFile, "Driver: Updated L2 TLB. Update time: %d\n", L2_TLB_UPDATE_TIME);

                    frameNum = TLBL2Search(requestedPageNum,&error);
                    time += L2_TLB_SEARCH_TIME;
                    fprintf(outputFile, "Driver: Re-searched through L2 TLB. Time cost: %d\n", L2_TLB_SEARCH_TIME);


                }

                TLBL1Update(requestedPageNum, frameNum);
                time += L1_TLB_UPDATE_TIME;
                fprintf(outputFile, "Driver: Updated L1 TLB. Update time: %d\n", L1_TLB_UPDATE_TIME);

                frameNum = TLBL1Search(requestedPageNum,&error);
                time += L1_TLB_SEARCH_TIME;
                fprintf(outputFile, "Driver: Re-searched through L1 TLB. Time cost: %d\n", L1_TLB_SEARCH_TIME);
            }   


            //Physical Addr (Frame Number) obtained. Now search for data
            unsigned int physicalAddr = (frameNum << 10) | (inputAddr & 0x3FF);


			// Split the frameNum into index, tag and offset according to both levels of cache's size.
			unsigned int l1CacheOffset = physicalAddr & 0x1F;
			unsigned int l1CacheIndex = (physicalAddr & 0x3FF) >> 5;
			unsigned int l1CacheTag = physicalAddr >> 10;

			unsigned int l2CacheOffset = physicalAddr & ((unsigned int)pow(2, 6) - 1);
			unsigned int l2CacheIndex = (physicalAddr & ((unsigned int)pow(2, 12) - 1)) >> 6;
			unsigned int l2CacheTag = physicalAddr >> 12;

            int retValue;
			if (write=='w')
			{
				// Since L1 cache is write through, the driver function only writes to the L1 cache.
				// And the writeL1Cache() calls writeL2Cache().
				retValue = writeL1Cache(l1CacheIndex, l1CacheTag, 0, dataCache);
                time += L1_CACHE_WRITE_TIME;
				
				if (retValue < 0)
				{
					//-------------------------------
					// Error Codes.
					if (retValue == ERROR_WRITE_FAILED_NO_TAG_MATCH)
                    {
                        fprintf(outputFile, "Driver: Write to L1 Cache Failed. Tag did not match. Time cost: %d\n", L1_CACHE_WRITE_TIME);
                        retValue = searchL2Cache(l2CacheIndex, l2CacheTag);
                        time += L2_CACHE_SEARCH_TIME;

                        if(retValue < 0)
                        {
                            fprintf(outputFile, "Driver: Search in L2 failed! Time cost: %d\n", L2_CACHE_SEARCH_TIME);
                            // Write to Main Memory
                            
        //Changed
                            int status = writeToMemory(physicalAddr>>10);
                            //Update Page Table Dirty Bit
                            pageTable* pT = getPageTableFromPid(i,0,1);
                            unsigned int indexOfLevel1PageTable = inputAddr>>18;
                            unsigned int level1Index = (inputAddr>>10) & 0x000000FF;
                            pT->frames[indexOfLevel1PageTable].entries[level1Index].modified = 1;
                            //

                            time += MAIN_MEMORY_WRITE_TIME;
                            fprintf(outputFile, "Driver: Write in Main Memory successfull! %d\n",MAIN_MEMORY_WRITE_TIME);


                            if(status == ERROR_WRITE_FAILED_NO_PERMISSION) 
                            {
                                fprintf(outputFile, "Driver: Error! write permission not for this memory address\n");

                                current_time += time;
                                pcbArr[i].runTime += time;
                                ++current_time;
                                continue;
                            }
                                
                            updateL2Cache(l2CacheIndex, l2CacheTag, write, 0);
                            time += L2_CACHE_UPDATE_TIME;
                            fprintf(outputFile, "Driver: Updated L2 Cache. Time cost: %d\n", L2_CACHE_WRITE_TIME);

                            updateL1Cache(l1CacheIndex, l1CacheTag, write, 0, dataCache);
                            time += L1_CACHE_UPDATE_TIME;
                            fprintf(outputFile, "Driver: Updated L1 Cache. Time cost: %d\n", L1_CACHE_WRITE_TIME);

                            writeL1Cache(l1CacheIndex, l1CacheTag, 0, dataCache);
                            time += L1_CACHE_WRITE_TIME;
                            fprintf(outputFile, "Driver: Write to L1 Cache successfully completed.\n");
                        }
                        else
                        {
                            fprintf(outputFile, "Driver: Search in L2 successfull! Search Time: %d\n", L2_CACHE_SEARCH_TIME);
                            updateL1Cache(l1CacheIndex, l1CacheTag, write, 0, dataCache);
                            time += L1_CACHE_UPDATE_TIME;
                            fprintf(outputFile, "Driver: Updated L1 Cache. Time cost: %d\n", L1_CACHE_WRITE_TIME);

                            writeL1Cache(l1CacheIndex, l1CacheTag, 0, dataCache);
                            time += L1_CACHE_WRITE_TIME;
                            fprintf(outputFile, "Driver: Write to L1 Cache successfully completed.\n");
                        }
                    }

					else if (retValue == ERROR_WRITE_FAILED_NO_PERMISSION)
                    {
                        fprintf(outputFile, "Driver: Error! write permission not for this memory address\n");
                    }

					else if (retValue == ERROR_CANNOT_WRITE_IN_INSTR_CACHE)
                    {
                        fprintf(outputFile, "Driver: Error! write permission not for this memory address\n");
                    }
					//-------------------------------
				}
				else
				{
					time += L1_CACHE_WRITE_TIME;
					fprintf(outputFile, "Driver: Write to L1 Cache successfully completed.\n");

                    current_time += time;
                    pcbArr[i].runTime += time;
                    ++current_time;
					continue;
				}
			}            
            else
            {
                //if instruction is read
                // Since L1 cache is look aside, we essentially search L1 and L2 cache simultaneously.
				// But this cannot be shown in the simulation, hence we consider the time taken to do the search to be MIN() of the two of them.
				// For look-through we would have added the time they take to search.
				int retValue1 = searchL1Cache(l1CacheIndex, l1CacheTag, dataCache);

				int retValue2 = searchL2Cache(l2CacheIndex, l2CacheTag);

				if (retValue1 >= 0 && retValue2 >= 0)
				{
					// Return the data to the processor.
					//return retValue;

					int previousActionTime = L1_CACHE_SEARCH_TIME;
					time += previousActionTime;

					fprintf(outputFile, "Driver: Found the required data in L1 cache.\n");
					fprintf(outputFile, "Search time taken: %d.\n", previousActionTime);
				}
				else if (retValue1 < 0 && retValue2 < 0)
				{
					// Not there in both the levels of cache.

					int previousActionTime = L2_CACHE_SEARCH_TIME;
					time += previousActionTime;

					fprintf(outputFile, "Driver: Did NOT find the required data in both L1 and L2 cache\n");
					fprintf(outputFile, "Search time taken: %d.\n", previousActionTime);
					fprintf(outputFile, "Driver: Will update L1 and L2 caches.\n");



					// search in the MM and update caches.
                    int status = readFromMemory(physicalAddr>>10);
                    if(status == -1)
                    {
                        fprintf(outputFile, "Driver: Error reading From Main Memory\n");
                        continue;
                    }
                    else
                    {
                        fprintf(outputFile, "Driver: Data read from Main Memory\n");
                    }

					//----------------------------
                    updateL2Cache(l2CacheIndex, l2CacheTag, write, 0);
					time += L2_CACHE_UPDATE_TIME;
					fprintf(outputFile, "Driver: L2 Cache update time, updating after data obtained from Main Memory: %d\n", L2_CACHE_UPDATE_TIME);

                    searchL2Cache(l2CacheIndex, l2CacheTag);
                    time += L2_CACHE_SEARCH_TIME;
					fprintf(outputFile, "Driver: L2 Cache search time, searching after data obtained from Main Memory: %d\n", L2_CACHE_SEARCH_TIME);


                    updateL1Cache(l1CacheIndex, l1CacheTag, write, 0, dataCache);
					time += L1_CACHE_UPDATE_TIME;
					fprintf(outputFile, "Driver: L1 Cache update time, updating after L2 updated and searched: %d\n", L1_CACHE_UPDATE_TIME);

                    searchL1Cache(l1CacheIndex, l1CacheTag, dataCache);
                    time += L1_CACHE_SEARCH_TIME;
					fprintf(outputFile, "Driver: L1 Cache search time, searching after L2 updated and searched: %d\n", L1_CACHE_SEARCH_TIME);

				}
				else if (retValue1 < 0 && retValue2 >= 0)
				{
					// Present in L2 cache, but not in L1.

					int previousActionTime = L2_CACHE_SEARCH_TIME;
					time += previousActionTime;
					fprintf(outputFile, "Driver: Did NOT find the required data in L1 cache, but found it in L2 cache.\n");
					fprintf(outputFile, "Search time taken: %d.\n", previousActionTime);
					fprintf(outputFile, "Driver: Will update L1 Cache.\n");

					// Update L1 cache.
                    updateL1Cache(l1CacheIndex, l1CacheTag, write, 0, dataCache);
					time += L1_CACHE_UPDATE_TIME;
					fprintf(outputFile, "Driver: L1 Cache update time: %d\n", L1_CACHE_UPDATE_TIME);

                    searchL1Cache(l1CacheIndex, l1CacheTag, dataCache);
                    time += L1_CACHE_SEARCH_TIME;
					fprintf(outputFile, "Driver: L1 Cache search time: %d\n", L1_CACHE_SEARCH_TIME);

			    }
			} // End of that memory access.

			// Updating the time taken by the previous memory access.
			current_time += time;
			pcbArr[i].runTime += time;
            ++current_time;
        }

    }
   // numProcessesAlive--;
    TLBL1Flush();
    TLBL2Flush();

    frameAgeing();
    // for(int i = 0; i < n; ++i)
    //     fclose(inputFile[i]);
}

fprintf(outputFile, "\n\n\nSIMULATION COMPLETED. ALL PROCESSES FINISHED EXECUTION.\n\n\n");
for(int i = 0; i < n; ++i)
{
    fclose(pcbArr[i].SegNumAddrFile);
    fclose(pcbArr[i].LinearAddrInputFile);
}

fclose(outputFile);
    
}