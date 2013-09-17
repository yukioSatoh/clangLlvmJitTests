#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/PassManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/Scalar.h"

#include <stdio.h>


static int Execute(llvm::Module *Mod)
{
	llvm::InitializeNativeTarget();
	std::string Error;
	llvm::OwningPtr<llvm::ExecutionEngine> EE(llvm::ExecutionEngine::createJIT(Mod, &Error, 0, llvm::CodeGenOpt::Aggressive));
	if (!EE)
	{
		llvm::errs() << "unable to make execution engine: " << Error << "\n";
		return 255;
	}

	//should get a better way to resolve the name? (c++filt like)
	llvm::Function *EntryFn = Mod->getFunction("_Z4testii");
	if (!EntryFn)
	{
		llvm::errs() << "'_Z4testii' function not found in module.\n";
		return 255;
	}


	// Validate the generated code, checking for consistency.
	llvm::verifyFunction(*EntryFn);

	//now further optimize here i guess
	llvm::FunctionPassManager OurFPM(Mod);

	// Set up the optimizer pipeline.  Start with registering info about how the
	// target lays out data structures.
	OurFPM.add(new llvm::DataLayout(*EE->getDataLayout()));
	// Provide basic AliasAnalysis support for GVN.
	OurFPM.add(llvm::createBasicAliasAnalysisPass());
	// Do simple "peephole" optimizations and bit-twiddling optzns.
	OurFPM.add(llvm::createInstructionCombiningPass());
	// Reassociate expressions.
	OurFPM.add(llvm::createReassociatePass());
	// Eliminate Common SubExpressions.
	OurFPM.add(llvm::createGVNPass());
	// Simplify the control flow graph (deleting unreachable blocks, etc).
	OurFPM.add(llvm::createCFGSimplificationPass());

	OurFPM.doInitialization();

	// Optimize the function.
	OurFPM.run(*EntryFn);

	std::vector<std::string> Args;
	Args.push_back(Mod->getModuleIdentifier());

	for(unsigned int i = 0; i < Args.size(); ++i)
	{
		printf("mod argumend[%i]: %s", i, Args[0].c_str());
	}

	void *FPtr = EE->getPointerToFunction(EntryFn);

	if(!FPtr)
	{
		llvm::errs() << "unable to get pointer to function.\n";
		return 1;
	}

//	A b;
//	b.b = 666;
//
//	void (*FP)(A) = (void (*)(A))(intptr_t)FPtr;
//
//	FP(b);


//	void (*FP)() = (void (*)())(intptr_t)FPtr;
//
//	FP();

	int (*FP)(int, int) = (int (*)(int, int))(intptr_t)FPtr;

	int c = FP(4, 30);
	printf("\n\nresult is %i\n", c);



	//cleanup
	EntryFn->eraseFromParent();

	EE->removeModule(Mod);

	delete Mod;


	return 0;
}


int main(int argc, char **argv)
{
	//*********************************************
	// D I A G N O S T I C S
	//Diagnostic used for error and warning!! its possible to create a DiagnosticClient for handling displays of it
	llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> DiagOpts = new clang::DiagnosticOptions();	//Options for controlling the compiler diagnostics engine.
	//the printing client
	clang::TextDiagnosticPrinter *DiagClient = new clang::TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);

	//Can be used and shared by multiple Diagnostics for multiple translation units.
	llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> pDiagIDs;
	//Concrete class used by the front-end to report problems and issues.
	clang::DiagnosticsEngine Diags(pDiagIDs, &*DiagOpts, DiagClient);    //had false at the end??

	//ins: _ClangExecutable, _DefaultTargetTriple, _DefaultImageName, _Diags
//	std::string Path = "/mnt/storage/workspaces/yuk_dev/llvm_testProject/src/clang_interpreter/aa";
	clang::driver::Driver TheDriver("", llvm::sys::getProcessTriple(), "a.out", Diags);    // Encapsulate logic for constructing compilation processes from a set of gcc-driver-like command line arguments
	TheDriver.setTitle("cppjittest");

	llvm::SmallVector<const char *, 16> Args(argv, argv + argc);
	Args.push_back("test.cpp");
	Args.push_back("-fsyntax-only");
	llvm::OwningPtr<clang::driver::Compilation> C(TheDriver.BuildCompilation(Args));    //Construct a compilation object for a command line argument vector.
	if (!C)
	{
		printf("unable to create compiler\n");
		return 0;
	}

	// We expect to get back exactly one command job, if we didn't something
	// failed. Extract that job from the compilation.
	const clang::driver::JobList &Jobs = C->getJobs();
	if (Jobs.size() != 1 || !llvm::isa<clang::driver::Command>(*Jobs.begin()))
	{
		llvm::SmallString<256> Msg;
		llvm::raw_svector_ostream OS(Msg);
		Jobs.Print(OS, "; ", true);
		Diags.Report(clang::diag::err_fe_expected_compiler_job) << OS.str();
		return 1;
	}

	const clang::driver::Command *Cmd = llvm::cast<clang::driver::Command>(*Jobs.begin());
	if (llvm::StringRef(Cmd->getCreator().getName()) != "clang")
	{
		Diags.Report(clang::diag::err_fe_expected_clang_command);
		return 1;
	}

	// Initialize a compiler invocation object from the clang (-cc1) arguments. helper class for holding the data necessary to invoke the compile
	const clang::driver::ArgStringList &CCArgs = Cmd->getArguments();

	llvm::OwningPtr<clang::CompilerInvocation> CI(new clang::CompilerInvocation);
	clang::CompilerInvocation::CreateFromArgs(*CI, const_cast<const char **>(CCArgs.data()),
		const_cast<const char **>(CCArgs.data()) + CCArgs.size(), Diags);

	//for now
	CI->getHeaderSearchOpts().Verbose = 1;

	// Show the invocation, with -v.
	if (CI->getHeaderSearchOpts().Verbose)
	{
		llvm::errs() << "clang invocation:\n";
		Jobs.Print(llvm::errs(), "\n", true);
		llvm::errs() << "\n";
	}


	// could -disable-free be the cause of the leak?
	for(unsigned int i = 0; i < Jobs.getJobs().size(); ++i)
	{
		clang::driver::Command *jb = static_cast<clang::driver::Command *>(Jobs.getJobs()[i]);

		for(unsigned int j = 0; j < jb->getArguments().size(); ++j)
		{
			if( jb->getArguments()[j] =="-disable-free")
			{
				printf("hmmmm %s\n", jb->getArguments()[j]);
			}
		}
	}


	// Create a compiler instance to handle the actual work.
	clang::CompilerInstance Clang;
	Clang.setInvocation(CI.take());

	// Create the compilers actual diagnostics engine.
	Clang.createDiagnostics();
	if (!Clang.hasDiagnostics())
	{
		printf("unable to create diagnostics");
		return 1;
	}

	// Infer the builtin include path if unspecified.
	if (Clang.getHeaderSearchOpts().UseBuiltinIncludes && Clang.getHeaderSearchOpts().ResourceDir.empty())
	{
		printf("no resources, should setup resource here i guess??\n");
		return 1;
	}

	//! @warning hardcoded path!!!!
	Clang.getHeaderSearchOpts().AddPath("/mnt/storage/development/cpp/llvm/checkout/install/lib/clang/3.4/include", clang::frontend::Angled, false, false);
	// ----//

	// Create and execute the frontend to generate an LLVM bitcode module.
	llvm::OwningPtr<clang::CodeGenAction> Act(new clang::EmitLLVMOnlyAction());
	if (!Clang.ExecuteAction(*Act))
		return 1;

	llvm::Module *Module = Act->takeModule();

	// Print all functions in the module
	for (llvm::Module::FunctionListType::iterator i = Module->getFunctionList().begin(); i != Module->getFunctionList().end(); ++i)
		printf("%s\n", i->getName().str().c_str());

	llvm::outs() << *Module;


	(void)Execute(Module);


	C->getJobs().clear();

	llvm::llvm_shutdown();

	return 0;
}
//make clean;clear;make
//env HEAPCHECK=normal ./main
//-disable-free

