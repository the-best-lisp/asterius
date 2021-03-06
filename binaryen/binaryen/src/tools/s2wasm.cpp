/*
 * Copyright 2015 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// s2wasm console tool
//

#include <exception>

#include "ir/trapping.h"
#include "support/colors.h"
#include "support/command-line.h"
#include "support/file.h"
#include "s2wasm.h"
#include "wasm-emscripten.h"
#include "wasm-io.h"
#include "wasm-linker.h"
#include "wasm-printing.h"
#include "wasm-validator.h"

using namespace cashew;
using namespace wasm;

int main(int argc, const char *argv[]) {
  bool ignoreUnknownSymbols = false;
  bool generateEmscriptenGlue = false;
  bool allowMemoryGrowth = false;
  bool importMemory = false;
  bool emitBinary = false;
  bool debugInfo = false;
  std::string startFunction;
  std::string sourceMapFilename;
  std::string sourceMapUrl;
  std::string symbolMap;
  std::vector<std::string> archiveLibraries;
  TrapMode trapMode = TrapMode::Allow;
  unsigned numReservedFunctionPointers = 0;
  Options options("s2wasm", "Link .s file into .wast");
  options.extra["validate"] = "wasm";
  options
      .add("--output", "-o", "Output file (stdout if not specified)",
           Options::Arguments::One,
           [](Options *o, const std::string& argument) {
             o->extra["output"] = argument;
             Colors::disable();
           })
      .add("--ignore-unknown", "", "Ignore unknown symbols",
           Options::Arguments::Zero,
           [&ignoreUnknownSymbols](Options *, const std::string& ) {
             ignoreUnknownSymbols = true;
           })
      .add("--start", "", "Generate the start method (default: main)",
           Options::Arguments::Optional,
           [&startFunction](Options *, const std::string& argument) {
             startFunction = argument.size() ? argument : "main";
           })
      .add("--global-base", "", "Where to start to place globals",
           Options::Arguments::One,
           [](Options *o, const std::string& argument) {
             o->extra["global-base"] = argument;
           })
      .add("--allocate-stack", "-s", "Size of the user stack in linear memory",
           Options::Arguments::One,
           [](Options *o, const std::string& argument) {
             o->extra["stack-allocation"] = argument;
           })
      .add("--initial-memory", "-i", "Initial size of the linear memory",
           Options::Arguments::One,
           [](Options *o, const std::string& argument) {
             o->extra["initial-memory"] = argument;
           })
      .add("--max-memory", "-m", "Maximum size of the linear memory",
           Options::Arguments::One,
           [](Options *o, const std::string& argument) {
             o->extra["max-memory"] = argument;
           })
      .add("--allow-memory-growth", "", "Allow linear memory to grow at runtime",
           Options::Arguments::Zero,
           [&allowMemoryGrowth](Options *, const std::string& ) {
             allowMemoryGrowth = true;
           })
      .add("--trap-mode", "",
           "Strategy for handling potentially trapping instructions. Valid "
             "values are \"allow\", \"js\", and \"clamp\"",
           Options::Arguments::One,
           [&trapMode](Options *o, const std::string& argument) {
             try {
               trapMode = trapModeFromString(argument);
             } catch (std::invalid_argument& e) {
               std::cerr << "Error: " << e.what() << "\n";
               exit(EXIT_FAILURE);
             }
           })
      .add("--emscripten-glue", "-e", "Generate emscripten glue",
           Options::Arguments::Zero,
           [&generateEmscriptenGlue](Options *, const std::string& ) {
             generateEmscriptenGlue = true;
           })
      .add("--import-memory", "", "Import the linear memory instead of exporting it",
           Options::Arguments::Zero,
           [&importMemory](Options *, const std::string& ) {
             importMemory = true;
           })
      .add("--library", "-l", "Add archive library",
           Options::Arguments::N,
           [&archiveLibraries](Options *o, const std::string& argument) {
             archiveLibraries.push_back(argument);
           })
      .add("--validate", "-v", "Control validation of the output module",
           Options::Arguments::One,
           [](Options *o, const std::string& argument) {
             if (argument != "web" && argument != "none" && argument != "wasm") {
               std::cerr << "Valid arguments for --validate flag are 'wasm', 'web' and 'none'.\n";
               exit(1);
             }
             o->extra["validate"] = argument;
           })
      .add("--emscripten-reserved-function-pointers", "",
           "Number of reserved function pointers for emscripten addFunction "
           "support",
           Options::Arguments::One,
           [&numReservedFunctionPointers](Options *o,
                                          const std::string &argument) {
             numReservedFunctionPointers = std::stoi(argument);
           })
      .add("--emit-binary", "",
           "Emit binary instead of text for the output file",
           Options::Arguments::Zero,
           [&emitBinary](Options *, const std::string &) {
             emitBinary = true;
           })
      .add("--debuginfo", "-g",
           "Emit names section in wasm binary (or full debuginfo in wast)",
           Options::Arguments::Zero,
           [&debugInfo](Options *, const std::string &) {
             debugInfo = true;
           })
      .add("--source-map", "-sm",
           "Emit source map (if using binary output) to the specified file",
           Options::Arguments::One,
           [&sourceMapFilename](Options *, const std::string& argument) {
             sourceMapFilename = argument;
           })
      .add("--source-map-url", "-su",
           "Use specified string as source map URL",
           Options::Arguments::One,
           [&sourceMapUrl](Options *, const std::string& argument) {
             sourceMapUrl = argument;
           })
      .add("--symbolmap", "-s",
           "Emit a symbol map (indexes => names)",
           Options::Arguments::One,
           [&symbolMap](Options *, const std::string& argument) {
             symbolMap = argument;
           })
      .add_positional("INFILE", Options::Arguments::One,
                      [](Options *o, const std::string& argument) {
                        o->extra["infile"] = argument;
                      });
  options.parse(argc, argv);

  if (options.extra["output"].size() == 0) {
    // when no output file is specified, we emit text to stdout
    emitBinary = false;
  }

  if (allowMemoryGrowth && !generateEmscriptenGlue) {
    Fatal() << "Error: adding memory growth code without Emscripten glue. "
      "This doesn't do anything.\n";
  }

  auto debugFlag = options.debug ? Flags::Debug : Flags::Release;
  auto input(read_file<std::string>(options.extra["infile"], Flags::Text, debugFlag));

  if (options.debug) std::cerr << "Parsing and wasming..." << std::endl;
  uint64_t globalBase = options.extra.find("global-base") != options.extra.end()
                          ? std::stoull(options.extra["global-base"])
                          : 0;
  uint64_t stackAllocation =
      options.extra.find("stack-allocation") != options.extra.end()
          ? std::stoull(options.extra["stack-allocation"])
          : 0;
  uint64_t initialMem =
      options.extra.find("initial-memory") != options.extra.end()
          ? std::stoull(options.extra["initial-memory"])
          : 0;
  uint64_t maxMem =
      options.extra.find("max-memory") != options.extra.end()
          ? std::stoull(options.extra["max-memory"])
          : 0;
  if (options.debug) std::cerr << "Global base " << globalBase << '\n';

  Linker linker(globalBase, stackAllocation, initialMem, maxMem,
                importMemory || generateEmscriptenGlue, ignoreUnknownSymbols, startFunction,
                options.debug);

  S2WasmBuilder mainbuilder(input.c_str(), options.debug);
  linker.linkObject(mainbuilder);

  if (trapMode != TrapMode::Allow) {
    Module* wasm = &(linker.getOutput().wasm);
    PassRunner runner(wasm);
    addTrapModePass(runner, trapMode);
    runner.run();
  }

  for (const auto& m : archiveLibraries) {
    auto archiveFile(read_file<std::vector<char>>(m, Flags::Binary, debugFlag));
    bool error;
    Archive lib(archiveFile, error);
    if (error) Fatal() << "Error opening archive " << m << "\n";
    linker.linkArchive(lib);
  }

  linker.layout();

  std::string metadata;
  Module& wasm = linker.getOutput().wasm;
  if (generateEmscriptenGlue) {
    if (options.debug) {
      std::cerr << "Emscripten gluing..." << std::endl;
      WasmPrinter::printModule(&wasm, std::cerr);
    }
    metadata = emscriptenGlue(
      wasm,
      allowMemoryGrowth,
      linker.getStackPointerAddress(),
      linker.getStaticBump(),
      linker.getOutput().getInitializerFunctions(),
      numReservedFunctionPointers);
  }

  if (options.extra["validate"] != "none") {
    if (options.debug) std::cerr << "Validating..." << std::endl;
    if (!wasm::WasmValidator().validate(wasm,
         WasmValidator::Globally | (options.extra["validate"] == "web" ? WasmValidator::Web : 0))) {
      WasmPrinter::printModule(&wasm);
      Fatal() << "Error: linked module is not valid.\n";
    }
  }

  if (options.debug) std::cerr << "Printing..." << std::endl;
  auto outputDebugFlag = options.debug ? Flags::Debug : Flags::Release;
  auto outputBinaryFlag = emitBinary ? Flags::Binary : Flags::Text;
  Output output(options.extra["output"], outputBinaryFlag, outputDebugFlag);

  ModuleWriter writer;
  writer.setDebug(options.debug);
  writer.setDebugInfo(debugInfo);
  writer.setSymbolMap(symbolMap);
  writer.setBinary(emitBinary);
  if (emitBinary) {
    writer.setSourceMapFilename(sourceMapFilename);
    writer.setSourceMapUrl(sourceMapUrl);
  }
  writer.write(wasm, output);

  if (generateEmscriptenGlue) {
    if (emitBinary) {
      std::cout << metadata;
    } else {
      output << ";; METADATA: " << metadata;
    }
  }

  if (options.debug) std::cerr << "Done." << std::endl;
  return 0;
}
