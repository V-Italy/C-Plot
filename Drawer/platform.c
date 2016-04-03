/* picoc's interface to the underlying platform. most platform-specific code
 * is in platform/platform_XX.c and platform/library_XX.c */
 
#include "picoc.h"
#include "interpreter.h"


/* initialise everything */
void PicocInitialise(Picoc *pc, int StackSize)
{
    memset(pc, '\0', sizeof(*pc));
    PlatformInit(pc);
    BasicIOInit(pc);
    HeapInit(pc, StackSize);
    TableInit(pc);
    VariableInit(pc);
    LexInit(pc);
    TypeInit(pc);
#ifndef NO_HASH_INCLUDE
    IncludeInit(pc);
#endif
    LibraryInit(pc);
#ifdef BUILTIN_MINI_STDLIB
    LibraryAdd(pc, &GlobalTable, "c library", &CLibrary[0]);
    CLibraryInit(pc);
#endif
    PlatformLibraryInit(pc);
    DebugInit(pc);
}

/* free memory */
void PicocCleanup(Picoc *pc)
{
    DebugCleanup(pc);
#ifndef NO_HASH_INCLUDE
    IncludeCleanup(pc);
#endif
    ParseCleanup(pc);
    LexCleanup(pc);
    VariableCleanup(pc);
    TypeCleanup(pc);
    TableStrFree(pc);
    HeapCleanup(pc);
    PlatformCleanup(pc);
}

/* platform-dependent code for running programs */
#if 1//defined(UNIX_HOST) || defined(WIN32)

#define CALL_MAIN_WITH_ARGS_RETURN_DOUBLE "__exit_value = main(__arg);"

void PicocCallMain(Picoc *pc, double arg)
{
    /* check if the program wants arguments */
    struct Value *FuncValue = NULL;

    if (!VariableDefined(pc, TableStrRegister(pc, "main")))
        ProgramFailNoParser(pc, "main() is not defined");
        
    VariableGet(pc, NULL, TableStrRegister(pc, "main"), &FuncValue);
    if (FuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(pc, "main is not a function - can't call it");

    if (FuncValue->Val->FuncDef.NumParams != 0)
    {
        /* define the arguments */
        VariableDefinePlatformVar(pc, NULL, "__arg", &pc->FPType, (union AnyValue *)&arg, FALSE);
    }

    if (FuncValue->Val->FuncDef.ReturnType != &pc->FPType)
    {
		ProgramFailNoParser(pc, "main function must return a double");
    }
    else
    {
        VariableDefinePlatformVar(pc, NULL, "__exit_value", &pc->FPType, (union AnyValue *)&pc->PicocExitValue, TRUE);
    
        if (FuncValue->Val->FuncDef.NumParams == 1)// && FuncValue->Val->FuncDef.ParamType == &pc->FPType)
			PicocParse(pc, "startup", CALL_MAIN_WITH_ARGS_RETURN_DOUBLE, strlen(CALL_MAIN_WITH_ARGS_RETURN_DOUBLE), TRUE, TRUE, FALSE, TRUE);
        else
			ProgramFailNoParser(pc, "main function must take a double as a param");
    }
}
#endif

void PrintSourceTextErrorLine(Picoc *pc, const char *FileName, const char *SourceText, int Line, int CharacterPos)
{
    int LineCount;
    const char *LinePos;
    const char *CPos;
    int CCount;
    
    if (SourceText != NULL)
    {
        /* find the source line */
        for (LinePos = SourceText, LineCount = 1; *LinePos != '\0' && LineCount < Line; LinePos++)
        {
            if (*LinePos == '\n')
                LineCount++;
        }
        
        /* display the line */
		for (CPos = LinePos; *CPos != '\n' && *CPos != '\0'; CPos++)
			PrintCh(*CPos, pc);
        PrintCh('\n', pc);
        
        /* display the error position */
        for (CPos = LinePos, CCount = 0; *CPos != '\n' && *CPos != '\0' && (CCount < CharacterPos || *CPos == ' '); CPos++, CCount++)
        {
            if (*CPos == '\t')
                PrintCh('\t', pc);
            else
                PrintCh(' ', pc);
        }
    }
    else
    {
        /* assume we're in interactive mode - try to make the arrow match up with the input text */
        for (CCount = 0; CCount < CharacterPos + (int)strlen(INTERACTIVE_PROMPT_STATEMENT); CCount++)
            PrintCh(' ', pc);
    }
    PlatformPrintf(pc, "line %d: ", Line);
    
}

/* exit with a message */
void ProgramFail(struct ParseState *Parser, const char *Message, ...)
{
    va_list Args;

    PrintSourceTextErrorLine(Parser->pc, Parser->FileName, Parser->SourceText, Parser->Line, Parser->CharacterPos);
    va_start(Args, Message);
	PlatformVPrintf(Parser->pc, Message, Args);
    va_end(Args);
    PlatformExit(Parser->pc, 1);
}

/* exit with a message, when we're not parsing a program */
void ProgramFailNoParser(Picoc *pc, const char *Message, ...)
{
    va_list Args;

    va_start(Args, Message);
    PlatformVPrintf(pc, Message, Args);
    va_end(Args);
    PlatformExit(pc, 1);
}

/* like ProgramFail() but gives descriptive error messages for assignment */
void AssignFail(struct ParseState *Parser, const char *Format, struct ValueType *Type1, struct ValueType *Type2, int Num1, int Num2, const char *FuncName, int ParamNo)
{
    PrintSourceTextErrorLine(Parser->pc, Parser->FileName, Parser->SourceText, Parser->Line, Parser->CharacterPos);
    PlatformPrintf(Parser->pc, "can't %s ", (FuncName == NULL) ? "assign" : "set");   
        
    if (Type1 != NULL)
        PlatformPrintf(Parser->pc, Format, Type1, Type2);
    else
        PlatformPrintf(Parser->pc, Format, Num1, Num2);
    
    if (FuncName != NULL)
        PlatformPrintf(Parser->pc, " in argument %d of call to %s()", ParamNo, FuncName);
    
    PlatformPrintf(Parser->pc, "\n");
    PlatformExit(Parser->pc, 1);
}

/* exit lexing with a message */
void LexFail(Picoc *pc, struct LexState *Lexer, const char *Message, ...)
{
    va_list Args;

    PrintSourceTextErrorLine(pc, Lexer->FileName, Lexer->SourceText, Lexer->Line, Lexer->CharacterPos);
    va_start(Args, Message);
    PlatformVPrintf(pc, Message, Args);
    va_end(Args);
    //PlatformPrintf(pc, "\n");
    PlatformExit(pc, 1);
}

/* printf for compiler error reporting */
void PlatformPrintf(Picoc *pc, const char *Format, ...)
{
    va_list Args;
    
    va_start(Args, Format);
    //PlatformVPrintf(Stream, Format, Args);
	
	if (pc->ErrorBufferLength < ERROR_BUFFER_SIZE - 1)
	{
		char* str = pc->ErrorBuffer + pc->ErrorBufferLength;
		pc->ErrorBufferLength += vsprintf_s(str, ERROR_BUFFER_SIZE - pc->ErrorBufferLength, Format, Args);
	}
    va_end(Args);
}

void PlatformVPrintf(Picoc *pc, const char *Format, va_list Args)
{	
	if (pc->ErrorBufferLength < ERROR_BUFFER_SIZE - 1)
	{
		char* str = pc->ErrorBuffer + pc->ErrorBufferLength;
		pc->ErrorBufferLength += vsprintf_s(str, ERROR_BUFFER_SIZE - pc->ErrorBufferLength, Format, Args);
	}
}

/*void PlatformVPrintf(IOFILE *Stream, const char *Format, va_list Args)
{
    const char *FPos;
    
    for (FPos = Format; *FPos != '\0'; FPos++)
    {
        if (*FPos == '%')
        {
            FPos++;
            switch (*FPos)
            {
            case 's': PrintStr(va_arg(Args, char *), Stream); break;
            case 'd': PrintSimpleInt(va_arg(Args, int), Stream); break;
            case 'c': PrintCh(va_arg(Args, int), Stream); break;
            case 't': PrintType(va_arg(Args, struct ValueType *), Stream); break;
#ifndef NO_FP
            case 'f': PrintFP(va_arg(Args, double), Stream); break;
#endif
            case '%': PrintCh('%', Stream); break;
            case '\0': FPos--; break;
            }
        }
        else
            PrintCh(*FPos, Stream);
    }
}*/

/* make a new temporary name. takes a static buffer of char [7] as a parameter. should be initialised to "XX0000"
 * where XX can be any characters */
char *PlatformMakeTempName(Picoc *pc, char *TempNameBuffer)
{
    int CPos = 5;
    
    while (CPos > 1)
    {
        if (TempNameBuffer[CPos] < '9')
        {
            TempNameBuffer[CPos]++;
            return TableStrRegister(pc, TempNameBuffer);
        }
        else
        {
            TempNameBuffer[CPos] = '0';
            CPos--;
        }
    }

    return TableStrRegister(pc, TempNameBuffer);
}
