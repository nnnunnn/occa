#include <occa/core/base.hpp>
#include <occa/internal/utils/env.hpp>
#include <occa/internal/io.hpp>
#include <occa/internal/utils/sys.hpp>
#include <occa/internal/modes/serial/device.hpp>
#include <occa/internal/modes/serial/kernel.hpp>
#include <occa/internal/modes/serial/buffer.hpp>
#include <occa/internal/modes/serial/memory.hpp>
#include <occa/internal/modes/serial/memoryPool.hpp>
#include <occa/internal/modes/serial/stream.hpp>
#include <occa/internal/modes/serial/streamTag.hpp>
#include <occa/internal/lang/modes/serial.hpp>

namespace occa {
  namespace serial {
    device::device(const occa::json &properties_) :
      occa::modeDevice_t(properties_) {}

    bool device::hasSeparateMemorySpace() const {
      return false;
    }

    hash_t device::hash() const {
      if (!hash_.initialized) {
        hash_ = occa::hash("host");
      }
      return hash_;
    }

    hash_t device::kernelHash(const occa::json &props) const {
      return (
        occa::hash(props["compiler"])
        ^ props["compiler_flags"]
        ^ props["compiler_env_script"]
        ^ props["compiler_vendor"]
        ^ props["compiler_language"]
        ^ props["compiler_linker_flags"]
        ^ props["compiler_shared_flags"]
      );
    }

    //---[ Stream ]---------------------
    modeStream_t* device::createStream(const occa::json &props) {
      return new stream(this, props);
    }

    modeStream_t* device::wrapStream(void* ptr, const occa::json &props) {
      return new stream(this, props);
    }

    occa::streamTag device::tagStream() {
      return new occa::serial::streamTag(this, sys::currentTime());
    }

    void device::waitFor(occa::streamTag tag) {}

    double device::timeBetween(const occa::streamTag &startTag,
                               const occa::streamTag &endTag) {
      occa::serial::streamTag *srStartTag = (
        dynamic_cast<occa::serial::streamTag*>(startTag.getModeStreamTag())
      );
      occa::serial::streamTag *srEndTag = (
        dynamic_cast<occa::serial::streamTag*>(endTag.getModeStreamTag())
      );

      return (srEndTag->time - srStartTag->time);
    }
    //==================================

    //---[ Kernel ]---------------------
    bool device::parseFile(const std::string &filename,
                           const std::string &outputFile,
                           const occa::json &kernelProps,
                           lang::sourceMetadata_t &metadata) {
      lang::okl::serialParser parser(kernelProps);
      parser.parseFile(filename);

      // Verify if parsing succeeded
      if (!parser.succeeded()) {
        OCCA_ERROR("Unable to transform OKL kernel [" << filename << "]",
                   kernelProps.get("silent", false));
        return false;
      }

      io::stageFile(
        outputFile,
        true,
        [&](const std::string &tempFilename) -> bool {
          parser.writeToFile(tempFilename);
          return true;
        }
      );

      parser.setSourceMetadata(metadata);

      return true;
    }

    // TODO: Functionally obsolete overload? kernelProps from the device will now be empty anyway.
    modeKernel_t* device::buildKernel(const std::string &filename,
                                      const std::string &kernelName,
                                      const hash_t kernelHash,
                                      const occa::json &kernelProps) {
      return buildKernel(filename, kernelName, kernelHash, kernelProps, false);
    }

    modeKernel_t* device::buildLauncherKernel(const std::string &filename,
                                              const std::string &kernelName,
                                              const hash_t kernelHash) {
      return buildKernel(filename, kernelName, kernelHash, properties["kernel"], true);
    }

    modeKernel_t* device::buildKernel(const std::string &filename,
                                      const std::string &kernelName,
                                      const hash_t kernelHash,
                                      const occa::json &kernelProps,
                                      const bool isLauncherKernel) {
      const std::string hashDir = io::hashDir(filename, kernelHash);

      const std::string &kcBinaryFile = (
        isLauncherKernel
        ? kc::launcherBinaryFile
        : kc::binaryFile
      );
      std::string binaryFilename = hashDir + kcBinaryFile;

      // Check if binary exists and is finished
      const bool foundBinary = io::isFile(binaryFilename);

      const bool verbose = kernelProps.get("verbose", false);
      if (foundBinary) {
        if (verbose) {
          io::stdout << "Loading cached ["
                     << kernelName
                     << "] from ["
                     << filename
                     << "] in [" << binaryFilename << "]\n";
        }
        modeKernel_t *k = buildKernelFromBinary(binaryFilename,
                                                kernelName,
                                                kernelProps);
        if (k) {
          k->sourceFilename = filename;
        }
        return k;
      }

      std::string compilerLanguage;
      std::string compiler;
      std::string compilerFlags;
      std::string compilerLinkerFlags;
      std::string compilerSharedFlags;
      std::string compilerEnvScript;

      // Default to C++
      compilerLanguage = "cpp";
      if (env::var("OCCA_COMPILER_LANGUAGE").size()) {
        compilerLanguage = env::var("OCCA_COMPILER_LANGUAGE");
      } else if (kernelProps.get<std::string>("compiler_language").size()) {
        compilerLanguage = (std::string) kernelProps["compiler_language"];
      }

      const bool compilingOkl = kernelProps.get("okl/enabled", true);
      const bool compilingCpp = compilingOkl || (lowercase(compilerLanguage) != "c");
      const int compilerLanguageFlag = (
        compilingCpp
        ? sys::language::CPP
        : sys::language::C
      );

      if (compilerLanguageFlag == sys::language::CPP && env::var("OCCA_CXX").size()) {
        compiler = env::var("OCCA_CXX");
      } else if (compilerLanguageFlag == sys::language::C && env::var("OCCA_CC").size()) {
        compiler = env::var("OCCA_CC");
      } else if (kernelProps.get<std::string>("compiler").size()) {
        compiler = (std::string) kernelProps["compiler"];
      } else if (compilerLanguageFlag == sys::language::CPP && env::var("CXX").size()) {
        compiler = env::var("CXX");
      } else if (compilerLanguageFlag == sys::language::C && env::var("CC").size()) {
        compiler = env::var("CC");
      } else if (compilerLanguageFlag == sys::language::CPP) {
#if (OCCA_OS & (OCCA_LINUX_OS | OCCA_MACOS_OS))
        compiler = "g++";
#else
        compiler = "cl.exe";
#endif
      } else {
#if (OCCA_OS & (OCCA_LINUX_OS | OCCA_MACOS_OS))
        compiler = "gcc";
#else
        compiler = "cl.exe";
#endif
      }

      if (kernelProps.get<std::string>("compiler_flags").size()) {
        compilerFlags = (std::string) kernelProps["compiler_flags"];
      } else if (compilerLanguageFlag == sys::language::CPP && env::var("OCCA_CXXFLAGS").size()) {
        compilerFlags = env::var("OCCA_CXXFLAGS");
      } else if (compilerLanguageFlag == sys::language::C && env::var("OCCA_CFLAGS").size()) {
        compilerFlags = env::var("OCCA_CFLAGS");
      } else if (compilerLanguageFlag == sys::language::CPP && env::var("CXXFLAGS").size()) {
        compilerFlags = env::var("CXXFLAGS");
      } else if (compilerLanguageFlag == sys::language::C && env::var("CFLAGS").size()) {
        compilerFlags = env::var("CFLAGS");
      } else {
#if (OCCA_OS & (OCCA_LINUX_OS | OCCA_MACOS_OS))
        compilerFlags = "-O3";
#else
        #ifdef NDEBUG
        compilerFlags = " /Ox";   // optimized
        #else
        compilerFlags = " /Od";   // debug
        #endif
#endif
      }

      const int compilerVendor = sys::compilerVendor(compiler);

      if (env::var("OCCA_COMPILER_SHARED_FLAGS").size()) {
        compilerSharedFlags = env::var("OCCA_COMPILER_SHARED_FLAGS");
      } else if (kernelProps.get<std::string>("compiler_shared_flags").size()) {
        compilerSharedFlags = (std::string) kernelProps["compiler_shared_flags"];
      } else {
        compilerSharedFlags = sys::compilerSharedBinaryFlags(compilerVendor);
      }

      if (env::var("OCCA_LDFLAGS").size()) {
        compilerLinkerFlags = env::var("OCCA_LDFLAGS");
      } else if (kernelProps.get<std::string>("compiler_linker_flags").size()) {
        compilerLinkerFlags = (std::string) kernelProps["compiler_linker_flags"];
      }

      if (kernelProps.get<std::string>("compiler_env_script").size()) {
        compilerEnvScript = (std::string) kernelProps["compiler_env_script"];
      } else {
#if (OCCA_USING_VS)
        compilerEnvScript = io::getVScompilerScript();  // NBN: see io/utils_win.cpp
#endif
      }

      if (compilerLanguageFlag == sys::language::CPP) {
        sys::addCompilerFlags(compilerFlags, sys::compilerCpp11Flags(compilerVendor));
      } else if (compilerLanguageFlag == sys::language::C) {
        sys::addCompilerFlags(compilerFlags, sys::compilerC99Flags(compilerVendor));
      }

      std::string sourceFilename;
      lang::sourceMetadata_t metadata;

      if (isLauncherKernel) {
        sourceFilename = filename;
      } else {
        const std::string &rawSourceFile = (
          compilingCpp
          ? kc::cppRawSourceFile
          : kc::cRawSourceFile
        );

        // Cache raw origin
        sourceFilename = (
          io::cacheFile(filename,
                        rawSourceFile,
                        kernelHash,
                        assembleKernelHeader(kernelProps))
        );

        if (compilingOkl) {
          const std::string outputFile = hashDir + kc::sourceFile;
          bool valid = parseFile(sourceFilename,
                                 outputFile,
                                 kernelProps,
                                 metadata);
          if (!valid) {
            return NULL;
          }
          sourceFilename = outputFile;

          writeKernelBuildFile(hashDir + kc::buildFile,
                               kernelHash,
                               kernelProps,
                               metadata);
        }
      }

      std::stringstream command;
      if (compilerEnvScript.size()) {
        command << compilerEnvScript << " && ";
      }

      sys::addCompilerFlags(compilerFlags, compilerSharedFlags);

      if (!compilingOkl) {
        sys::addCompilerIncludeFlags(compilerFlags);
        sys::addCompilerLibraryFlags(compilerFlags);
      }

      io::stageFile(
        binaryFilename,
        true,
        [&](const std::string &tempFilename) -> bool {
#if (OCCA_OS & (OCCA_LINUX_OS | OCCA_MACOS_OS))
          command << compiler
                  << ' '    << compilerFlags
                  << ' '    << sourceFilename
                  << " -o " << tempFilename
                  << " -I"  << env::OCCA_DIR << "include"
                  << " -I"  << env::OCCA_INSTALL_DIR << "include"
                  << " -L"  << env::OCCA_INSTALL_DIR << "lib -locca"
                  << ' '    << compilerLinkerFlags
                  << " 2>&1"
                  << std::endl;
#else
          // NBN: compiler fails if not linked with cuda.lib ??
          const std::string cudaLib = "C:/VS/CUDA/lib/x64/cuda.lib";

          #ifdef NDEBUG
          const std::string occaLib = env::OCCA_DIR + "lib/libocca.lib";
          #else
          const std::string occaLib = env::OCCA_DIR + "lib/libocca_d.lib"; // NBN: for debugging
          #endif

          command << compiler
                  << ' '       << compilerFlags
                  << ' '       << sourceFilename
                  << " -I"     << env::OCCA_DIR << "include"
                  << " /link " << cudaLib
                  << ' '       << occaLib
                  << " /out:"  << tempFilename
                  << std::ends;

#endif

          const std::string &sCommand = strip(command.str());
          if (verbose) {
            io::stdout << "Compiling [" << kernelName << "]\n" << sCommand << "\n";
          }

          std::string commandOutput;
          const int commandExitCode = sys::call(
            sCommand.c_str(),
            commandOutput
          );

          if (commandExitCode) {
            OCCA_FORCE_ERROR(
              "Error compiling [" << kernelName << "],"
              " Command: [" << sCommand << "]\n"
              << "Output:\n\n"
              << commandOutput << "\n"
            );
          }

          io::sync(binaryFilename);
          return true;
        }
      );

      modeKernel_t *k = buildKernelFromBinary(binaryFilename,
                                              kernelName,
                                              kernelProps,
                                              metadata.kernelsMetadata[kernelName]);
      if (k) {
        k->sourceFilename = filename;
      }
      return k;
    }

    modeKernel_t* device::buildKernelFromBinary(const std::string &filename,
                                                const std::string &kernelName,
                                                const occa::json &kernelProps) {
      std::string buildFile = io::dirname(filename);
      buildFile += kc::buildFile;

      lang::kernelMetadata_t metadata;
      if (io::isFile(buildFile)) {
        lang::sourceMetadata_t sourceMetadata = lang::sourceMetadata_t::fromBuildFile(buildFile);
        metadata = sourceMetadata.kernelsMetadata[kernelName];
      }

      return buildKernelFromBinary(filename,
                                   kernelName,
                                   kernelProps,
                                   metadata);
    }

    modeKernel_t* device::buildKernelFromBinary(const std::string &filename,
                                                const std::string &kernelName,
                                                const occa::json &kernelProps,
                                                lang::kernelMetadata_t &metadata) {
      kernel &k = *(new kernel(this,
                               kernelName,
                               filename,
                               kernelProps));

      k.binaryFilename = filename;
      k.metadata = metadata;

      k.dlHandle = sys::dlopen(filename);
      k.function = sys::dlsym(k.dlHandle, kernelName);

      return &k;
    }
    //==================================

    //---[ Memory ]-------------------
    modeMemory_t* device::malloc(const udim_t bytes,
                                 const void *src,
                                 const occa::json &props) {
      //create allocation
      buffer *buf = new serial::buffer(this, bytes, props);

      if (src && props.get("use_host_pointer", false)) {
        buf->wrapMemory(src, bytes);
      } else {
        buf->malloc(bytes);
      }

      //create slice
      memory *mem = new serial::memory(buf, bytes, 0);

      if (src && !props.get("use_host_pointer", false)) {
        mem->copyFrom(src, bytes, 0, props);
      }

      return mem;
    }

    modeMemory_t* device::wrapMemory(const void *ptr,
                                     const udim_t bytes,
                                     const occa::json &props) {

      //create allocation
      buffer *buf = new serial::buffer(this, bytes, props);
      buf->wrapMemory(ptr, bytes);

      return new serial::memory(buf, bytes, 0);
    }

    modeMemoryPool_t* device::createMemoryPool(const occa::json &props) {
      return new serial::memoryPool(this, props);
    }

    udim_t device::memorySize() const {
      return sys::SystemInfo::load().memory.total;
    }
    //==================================

    void* device::unwrap() {
      OCCA_FORCE_ERROR("device::unwrap is not defined for serial mode");
      return nullptr;
    }
  }
}
