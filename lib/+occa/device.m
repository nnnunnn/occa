classdef device < handle
   properties
       isAllocated = 0
       cDevice    
   end
   
   methods (Static)      
       function deviceCountFunction(arg)
           persistent counter;
           
           if arg == 1
               if isempty(counter)
                   counter = 1;
               else
                   counter = counter + 1;
               end
           elseif arg == 0
               counter = counter - 1;
           end
           
           if counter == 0
               unloadlibrary('libocca');
           end
       end
   end
      
   methods
       function mode_ = mode(this)
           mode_ = calllib('libocca', 'occaDeviceMode', this.cDevice);
       end
       
       function this = device(mode, arg1, arg2)
           occa.init()
                      
           this.deviceCountFunction(1)
           
           this.cDevice     = calllib('libocca', 'occaGetDevice', mode, arg1, arg2);
           this.isAllocated = 1;           
       end
       
       function setup(this, mode, arg1, arg2)
           occa.init()
           
           this.deviceCountFunction(1)
           
           this.cDevice     = calllib('libocca', 'occaGetDevice', mode, arg1, arg2);
           this.isAllocated = 1;
       end
       
       function setCompiler(this, compiler)
           calllib('libocca', 'occaDeviceSetCompiler', this.cDevice, compiler);
       end
       
       function setCompilerFlags(this, compilerFlags)
           calllib('libocca', 'occaDeviceSetCompilerFlags', this.cDevice, compilerFlags);
       end
       
       function kernel_ = buildKernelFromSource(this, filename, functionName, info)           
           kernel_.cKernel = calllib('libocca', 'occaBuildKernelFromSource', this.cDevice, ...
                                                                             filename,     ...
                                                                             functionName, ...
                                                                             info);
           kernel_.isAllocated = 1;
       end
       
       function kernel_ = buildKernelFromBinary(this, filename, functionName)
           kernel_.cKernel = calllib('libocca', 'occaBuildKernelFromBinary', this.cDevice, ...
                                                                             filename,     ...
                                                                             functionName);
           kernel_.isAllocated = 1;                                                                             
       end
       
       function memory_ = malloc(this, entries, entryType)           
           memory_.cMemory = calllib('libocca', 'occaDeviceMalloc', this.cDevice, entries, entryType);
           memory_.isAllocated = 1;
       end
       
       function stream_ = genStream(this)
           stream_.cStream = calllib('libocca', 'occaGenStream', this.cDevice);
       end
       
       function stream_ = getStream(this)
           stream_.cStream = calllib('libocca', 'occaGetStream', this.cDevice);
       end
       
       function setStream(this, stream_)
           calllib('libocca', 'occaSetStream', this.cDevice, stream_.cStream);
       end
       
       function free(this)
           if this.isAllocated              
               calllib('libocca', 'occaDeviceFree', this.cDevice);
               this.deviceCountFunction(0)
           end
       end
       
       function delete(this)
           this.free()
       end
   end
end