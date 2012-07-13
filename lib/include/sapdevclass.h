/*
Licence


v1: 19-aug-2010

Class that holds the sapporo device-structure which contains 
all device memory references, kernels, and accompanying functions
to allocate, load, start functions, etc.


*/


enum { GRAPE5   = 0, FOURTH, SIXTH, EIGHT};        //0, 1, 2, 3
enum { DEFAULT  = 0, DOUBLE}; //defualt is 0, double precision 1

#ifdef _OCL_
  #include "include/ocldev.h"

  typedef cl_float2 float2;
  typedef cl_float4 float4;

  typedef cl_double4 double4;
  typedef cl_double2 double2;

  typedef cl_int4 int4;

#else
  #include "include/cudadev.h"
#endif

#include <cassert>
#include <iostream>
#include <omp.h>

#include <sys/time.h>

#include "include/defines.h"

namespace sapporo2 {

  class device
  {
    private:
      //Flags
      bool hasDevice;
      
      //Context related
      dev::context    context;
      
      //Number of multiprocessors and number of blocks per SM
      int nMulti;      
      int NBLOCKS;
      
    public:      
      //Kernels
      dev::kernel     copyJParticles;
      dev::kernel     predictKernel;
      dev::kernel     evalgravKernel;
      dev::kernel     reduceForces;
      

      //Memory
      //j-particles, buffers used in computations
      dev::memory<double4> pPos_j;       //Predicted position       
      dev::memory<double4> pVel_j;       //Predicted velocity
      dev::memory<double4> pAcc_j;       //Predicted acceleration, for 6thand 8th order
      
      dev::memory<int>    address_j;     //Address
      dev::memory<double2> t_j;          //Times
      dev::memory<double4> pos_j;        //position       
      dev::memory<double4> vel_j;        //velocity
      dev::memory<double4> acc_j;        //acceleration
      dev::memory<double4> jrk_j;        //jerk      
      dev::memory<double4> snp_j;        //Snap for 6th and 8th order
      dev::memory<double4> crk_j;        //Crack for 6th and 8th order
      dev::memory<int>     id_j;         //Particle ID
      
      //Data is copied from the temp buffers to buffers used in the computations
      //the set of buffers defined here above)
      dev::memory<double2> t_j_temp;          //Temp Times
      dev::memory<double4> pos_j_temp;        //Temp position-j
      dev::memory<double4> acc_j_temp;        //Temp acceleration-j
      dev::memory<double4> vel_j_temp;        //Temp velocity-j
      dev::memory<double4> jrk_j_temp;        //Temp Jerk-j      
      dev::memory<double4> snp_j_temp;        //Temp Snap for 6th and 8th order
      dev::memory<double4> crk_j_temp;        //Temp Crack for 6th and 8th order
      dev::memory<int>     id_j_temp;         //Temp id-j
      
      //i-particle buffers
      //in variables
      dev::memory<double4> pos_i;        //position       
      dev::memory<double4> vel_i;        //velocity
      dev::memory<double4> accin_i;      //acceleration for 6th and 8th order
      
      //out variables
      dev::memory<double4> acc_i;        //acceleration
      dev::memory<double4> jrk_i;        //jerk
      dev::memory<double4> snp_i;        //Snap for 6th and 8th order
      dev::memory<double4> crk_i;        //Crack for 6th and 8th order
      dev::memory<double>  ds_i;         //minimal distance
      dev::memory<int>     id_i;         //particle id
      
      dev::memory<int>   ngb_list_i;  //neighbour list
      
      int dev_ni;                       //Number of ni particles on the device
      int nj_local;                     //Number of particles on this device
      
      int integrationOrder;             //Which integration algorithm do we use. Needed for shmem config

    public:
      //Functions
      
      int get_NBLOCKS()
      {
        return NBLOCKS;
      }
      
      //Assign a device and start the context
      int assignDevice(int devID, int order = 0)
      {
        integrationOrder = order;
        cerr << "integrationOrder : " << integrationOrder << endl;        
        
        #ifdef __OPENCL_DEV__
//           const int numPlatform = context.getPlatformInfo();
          context.getDeviceCount(CL_DEVICE_TYPE_GPU, 0);
        #else
          context.getDeviceCount();
        #endif
        context.createQueue(devID);    
        
        //Get the number of multi-processors on the device
        nMulti = context.get_numberOfMultiProcessors(); 
        
        //Get number of blocks per multiprocessor and compute total number of thread-blocks to use
        int blocksPerMulti = getBlocksPerSM(context.getComputeCapabilityMajor(),
                                            context.getComputeCapabilityMinor(),
                                            context.getDeviceName());
        
        NBLOCKS = nMulti*blocksPerMulti;
        cerr << "Using  " << blocksPerMulti << " blocks per multi-processor for a total of : " << NBLOCKS << std::endl;
        //NBLOCKS = 1 ;
        
        
        //Set the device memory contexts
        
        //J-particle buffers
        pPos_j.setContext(context);     pVel_j.setContext(context);
        address_j.setContext(context);  t_j.setContext(context);
        pos_j.setContext(context);      vel_j.setContext(context);
        acc_j.setContext(context);      jrk_j.setContext(context);
        snp_j.setContext(context);      crk_j.setContext(context);
        id_j.setContext(context);       pAcc_j.setContext(context);
        
        //Temporary J-particle buffers
        t_j_temp.setContext(context);     id_j_temp.setContext(context);    
        pos_j_temp.setContext(context);   vel_j_temp.setContext(context);
        acc_j_temp.setContext(context);   jrk_j_temp.setContext(context); 
        snp_j_temp.setContext(context);   crk_j_temp.setContext(context); 
        
        //i-particle buffers
        pos_i.setContext(context);      vel_i.setContext(context);
        acc_i.setContext(context);      jrk_i.setContext(context);
        ds_i.setContext(context);       ngb_list_i.setContext(context);
        id_i.setContext(context);       accin_i.setContext(context);
        snp_i.setContext(context);      crk_i.setContext(context);        
        
        return 0;
      }
      
      //Load the kernels
      int loadComputeKernels(const char *filename)
      {
        //Assign context and load the source file
        copyJParticles.setContext(context);
        predictKernel.setContext(context);
        evalgravKernel.setContext(context);
        reduceForces.setContext(context);
	  
        copyJParticles.load_source(filename, "");
        predictKernel.load_source(filename, "");
        evalgravKernel.load_source(filename, "");
        reduceForces.load_source(filename, "");
	  
        cerr << "Kernel files found .. building compute kernels! \n";
  
        copyJParticles.create("dev_copy_particles");
        predictKernel.create("dev_predictor");
        evalgravKernel.create("dev_evaluate_gravity");
        reduceForces.create("dev_reduce_forces");
        
       
        return 0;
      }
      
      
      //Allocate memory
      int allocateMemory(int nj, int n_pipes)
      {
        nj_local = nj;
  
       
        //J-particle allocation
        pPos_j.allocate(nj, false);                    
        pos_j.allocate(nj, false);   
        address_j.allocate(nj, false);      
        

        if(integrationOrder > GRAPE5)
        {
          acc_j.allocate(nj, false);
          jrk_j.allocate(nj, false); 
          id_j.allocate(nj, false);
          t_j.allocate(nj, false);       
          vel_j.allocate(nj, false); 
          pVel_j.allocate(nj, false);
        
          if(integrationOrder > FOURTH)
          {
            snp_j.allocate(nj, false);    
            crk_j.allocate(nj, false); 
            pAcc_j.allocate(nj, false); 
          }
        }
        
        //TODO make this pinned memory since the communicate with the host        
        pos_j_temp.allocate(nj, false);       
        

        if(integrationOrder > GRAPE5)
        {
          t_j_temp.allocate(nj, false);         id_j_temp.allocate(nj, false);  
          acc_j_temp.allocate(nj, false);       jrk_j_temp.allocate(nj, false); 
          vel_j_temp.allocate(nj, false); 
        
          if(integrationOrder > FOURTH)
          {
            snp_j_temp.allocate(nj, false);       crk_j_temp.allocate(nj, false); 
          }                
        }
        
        //i particle TODO copied it from sap1, need to check how and why
        //TODO make this pinned memory since the communicate with the host
        pos_i.allocate(n_pipes, false);   
        
        acc_i.allocate(n_pipes *      NBLOCKS, false, false);   
        accin_i.allocate(n_pipes *    NBLOCKS, false, false);   

        if(integrationOrder > GRAPE5)
        {
          vel_i.allocate(n_pipes * (1 + NBLOCKS), false);   
          jrk_i.allocate(n_pipes *      NBLOCKS, false);
          id_i.allocate (n_pipes *      NBLOCKS, false);   
          ds_i.allocate(n_pipes, false);             
      
          if(integrationOrder > FOURTH)
          {
            snp_i.allocate(n_pipes *      NBLOCKS, false);
            crk_i.allocate(n_pipes *      NBLOCKS, false);          
          }  
        }  
        

//         int ngbMem =  (n_pipes*(NGB_PP + 1) + n_pipes*NBLOCKS*(NGB_PP+1)); //TODO check / change!
        int ngbMem =  (NTHREADS*(NGB_PP + 1) + NTHREADS*NBLOCKS*(NGB_PP+1)); //TODO check / change!
        ngb_list_i.allocate(ngbMem, false);   
        
       return 0;
      }
      
      int reallocJParticles(int nj)
      {
        //J-particle allocation
        pPos_j.realloc(nj, false);  
        pos_j.realloc(nj, false);       
        address_j.realloc(nj, false, false);  //Host content is newer than device so no copy
        if(integrationOrder > GRAPE5)
        {
          pVel_j.realloc(nj, false);
        
          t_j.realloc(nj, false);         id_j.realloc(nj, false);
          vel_j.realloc(nj, false); 
          acc_j.realloc(nj, false);       jrk_j.realloc(nj, false); 
          if(integrationOrder > FOURTH)
          {
            snp_j.realloc(nj, false);       crk_j.realloc(nj, false); 
            pAcc_j.allocate(nj, false);       
          }   
        }        
        //TODO make this pinned memory since they communicate with the host
        
        //3rd arguments is false since data on the host is newer than on the device
        pos_j_temp.realloc(nj, false, false);       
        
        if(integrationOrder > GRAPE5)
        {
          t_j_temp.realloc(nj, false,   false);         id_j_temp.realloc(nj, false, false);  
          vel_j_temp.realloc(nj, false, false); 
          acc_j_temp.realloc(nj, false, false);       jrk_j_temp.realloc(nj, false , false); 
        
          if(integrationOrder > FOURTH)
          {
            snp_j_temp.realloc(nj, false, false);       crk_j_temp.realloc(nj, false, false); 
          }  
        }        
        
        return 0;
      }
      
  }; //end class

}//end namespace
