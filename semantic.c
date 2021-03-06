#include "semantic.h"
#include "tree.h"
#include <assert.h>
#include <string.h>
int numHashSearch = 0;
int hashType = 1;
struct Variable *variableTable[TABLE_SIZE];
struct Structure *structureTable[TABLE_SIZE];
struct Func *funcTable[TABLE_SIZE];
struct Pair *pairTable[TABLE_SIZE];

struct InterCode *InterCodeList = NULL;

struct Type *intTypePtr = NULL;

//中间代码中临时变量数量
int nrTmpVar = 0;
//中间代码中跳转标号数量
int nrLabel = 0;
//当前函数局部变量所占空间,4的倍数
int curSpace = 0;

int calculateWidth(struct Type *typePtr)
{
    assert(typePtr != NULL);
    //不用到width属性
    int sumWidth = 0;
    switch (typePtr->kind)
    {
    case BASIC:
    {
        sumWidth = INT_FLOAT_SIZE;
    }
    break;
    case ARRAY:
    {
        //计算维数乘积
        int product = 1;
        while (typePtr->kind == ARRAY)
        {
            product = product * typePtr->info.array->numElem;
            typePtr = typePtr->info.array->elem;
        }
        sumWidth = product * calculateWidth(typePtr);
    }
    break;
    case STRUCTURE:
    {
        struct FieldList *ptr = typePtr->info.structure->structureInfo;
        while (ptr != NULL)
        {
            sumWidth += calculateWidth(ptr->type);
            ptr = ptr->next;
        }
    }
    break;
    }
    return sumWidth;
}

void reverseArray(struct Type *arrayTypePtr)
{
    assert(arrayTypePtr != NULL);
    assert(arrayTypePtr->kind == ARRAY);
    //先计算总维数
    int numDim = 0;
    struct Type *curType = arrayTypePtr;
    while ((curType != NULL) && (curType->kind == ARRAY))
    {
        numDim++;
        curType = curType->info.array->elem;
    }
    assert(curType != NULL);
    int baseWidth = calculateWidth(curType); //基类型宽度
    if (numDim == 1)
    { // 一维数组
        arrayTypePtr->info.array->elemWidth = baseWidth;
        return;
    }
    //高维
    int *dimSizes = (int *)malloc(sizeof(int) * numDim);
    int *dimWidths = (int *)malloc(sizeof(int) * numDim);
    int curDim = numDim - 1;
    curType = arrayTypePtr;
    while (curType->kind == ARRAY)
    {
        dimSizes[curDim] = curType->info.array->numElem;
        curDim--;
        curType = curType->info.array->elem;
    }

    assert(curDim == -1);

    dimWidths[numDim - 1] = baseWidth;
    for (int i = numDim - 2; i >= 0; --i)
    {
        dimWidths[i] = dimSizes[i + 1] * dimWidths[i + 1];
    }

    curDim = 0;
    curType = arrayTypePtr;
    while (curType->kind == ARRAY)
    {
        curType->info.array->numElem = dimSizes[curDim];
        curType->info.array->elemWidth = dimWidths[curDim];
        ++curDim;
        curType = curType->info.array->elem;
    }
    assert(curDim == numDim);

    free(dimSizes); //释放空间
    free(dimWidths);
}

void printError(int type, int line, char *msg)
{
    printf("Error Type %d at Line %d: %s\n", type, line, msg);
}

void printFuncDec(struct Func *funcPtr, int nrSpace)
{
    assert(funcPtr != NULL);
    printSpace(nrSpace);
    printf("function name:%s\n", funcPtr->name);
    printSpace(nrSpace);
    printf("function return type:\n");
    printType(funcPtr->retType, nrSpace + 2);
    printSpace(nrSpace);
    printf("function parameters:\n");
    printFieldList(funcPtr->params, nrSpace + 2);
}

unsigned hash(char *name, int sz)
{
    // unsigned val = 0, i;
    // for (; *name; ++name)
    // {
    //     val = (val << 2) + *name;
    //     if (i = val & ~sz)
    //         val = (val ^ (i >> 12)) & sz;
    // }
    // return val;

    if (hashType == 1)
    {
        unsigned val = 0;
        for (; *name; ++name)
        {
            val += *name;
        }
        return val % sz;
    }

    else
    {
        unsigned int hash = 0;
        unsigned int x = 0;

        while (*name)
        {
            hash = (hash << 4) + (*name++); //hash左移4位，把当前字符ASCII存入hash低四位。
            if ((x = hash & 0xF0000000L) != 0)
            {
                //如果最高的四位不为0，则说明字符多余7个，现在正在存第7个字符，如果不处理，再加下一个字符时，第一个字符会被移出，因此要有如下处理。
                //该处理，如果最高位为0，就会仅仅影响5-8位，否则会影响5-31位，因为C语言使用的算数移位
                //因为1-4位刚刚存储了新加入到字符，所以不能>>28
                hash ^= (x >> 24);
                //上面这行代码并不会对X有影响，本身X和hash的高4位相同，下面这行代码&~即对28-31(高4位)位清零。
                hash &= ~x;
            }
        }
        //返回一个符号位为0的数，即丢弃最高位，以免函数外产生影响。(我们可以考虑，如果只有字符，符号位不可能为负)
        return (hash & 0x7FFFFFFF) % sz;
    }
}

int checkTypeSame(struct Type *typePtr1, struct Type *typePtr2)
{
    //TODO 判断类型等价
    //先检查是否为NULL!
    //如果有一个是NULL，说明类型错误
    if ((typePtr1 == NULL) || (typePtr2 == NULL))
    {
        return 0;
    }
    if (typePtr1 == typePtr2)
    {
        //对于structure，如果指向同一个结构，那么就同类
        //也只有structure可能相同
        return 1;
    }
    if (typePtr1->kind != typePtr2->kind)
    {
        return 0; //类型都不同
    }
    else
    {
        //现在同类
        if (typePtr1->kind == BASIC)
        {
            return typePtr1->info.basicType == typePtr2->info.basicType;
        }
        else if (typePtr1->kind == ARRAY)
        {
            //维数相同就行
            //递归
            return checkTypeSame(typePtr1->info.array->elem, typePtr2->info.array->elem);
        }
        else
        {
            //结构等价,就是每个域都等价
            struct FieldList *FL1 = typePtr1->info.structure->structureInfo;
            struct FieldList *FL2 = typePtr2->info.structure->structureInfo;
            while ((FL1 != NULL) && (FL2 != NULL))
            {
                //有一个不等价就不行
                if (checkTypeSame(FL1->type, FL2->type) == 0)
                {
                    return 0;
                }
                FL1 = FL1->next;
                FL2 = FL2->next;
            }
            if ((FL1 == NULL) && (FL2 == NULL))
            {
                return 1; //前面全都等价
            }
            else
            {
                //域的数量不一致
                return 0;
            }
        }
    }
}

void checkNoDuplicateName(struct FieldList *FL)
{
    //用一个hashSet,自己写
    int sz = 10;
    struct NameNode **set = getNameHashSet(sz); //大小自己定
    while (FL != NULL)
    {
        if (nameSetContains(set, sz, FL->name))
        {
            printError(15, FL->line, "Structure field redifined.");
        }
        else
        {
            insertIntoSet(set, sz, FL->name);
        }
        FL = FL->next;
    }
    freeSet(set, sz);
}

int checkFieldListSame(struct FieldList *FL1, struct FieldList *FL2)
{
    //判断两个FL是否等价，注意FL可以为NULL，说明参数列表为空
    while ((FL1 != NULL) && (FL2 != NULL))
    {
        if (checkTypeSame(FL1->type, FL2->type) == 0)
        {
            return 0;
        }
        FL1 = FL1->next;
        FL2 = FL2->next;
    }
    if ((FL1 == NULL) && (FL2 == NULL))
    {
        return 1; //前面全都等价
    }
    else
    {
        //参数的数量不一致
        return 0;
    }
}

void insertFuncTable(struct Func *func)
{
    assert(func != NULL);
    assert(func->name != NULL);
    unsigned idx = hash(func->name, TABLE_SIZE);
    struct Func *head = funcTable[idx];
    //头部插入
    func->next = head;
    funcTable[idx] = func;
}

void insertStructureTable(struct Structure *structure)
{
    assert(structure != NULL);
    assert(structure->name != NULL);
    unsigned idx = hash(structure->name, TABLE_SIZE);
    struct Structure *head = structureTable[idx];
    //头部插入
    structure->next = head;
    structureTable[idx] = structure;
}

void insertPairTable(struct Pair *pairPtr)
{
    assert(pairPtr != NULL);
    unsigned idx = pairPtr->tmpNo % TABLE_SIZE;
    struct Pair *head = pairTable[idx];
    pairPtr->next = head;
    pairTable[idx] = pairPtr;
}

void insertVariableTable(struct Variable *variable)
{
    assert(variable != NULL);
    assert(variable->name != NULL);
    unsigned idx = hash(variable->name, TABLE_SIZE);
    struct Variable *head = variableTable[idx];
    //头部插入
    variable->next = head;
    variableTable[idx] = variable;
}

struct Func *searchFuncTable(char *name)
{
    assert(name != NULL);
    unsigned idx = hash(name, TABLE_SIZE);
    struct Func *head = funcTable[idx];
    numHashSearch++;
    while (head != NULL && (strcmp(name, head->name) != 0))
    {
        head = head->next;
        numHashSearch++;
    }
    return head;
}

struct Pair *searchPairTable(int tmpNo)
{
    unsigned idx = tmpNo % TABLE_SIZE;
    struct Pair *head = pairTable[idx];
    while (head != NULL && head->tmpNo != tmpNo)
    {
        head = head->next;
    }
    return head;
}

struct Structure *searchStructureTable(char *name)
{
    assert(name != NULL);
    unsigned idx = hash(name, TABLE_SIZE);
    struct Structure *head = structureTable[idx];
    numHashSearch++;
    while (head != NULL && (strcmp(name, head->name) != 0))
    {
        numHashSearch++;
        head = head->next;
    }
    return head;
}

struct Variable *searchVariableTable(char *name)
{
    assert(name != NULL);
    unsigned idx = hash(name, TABLE_SIZE);
    struct Variable *head = variableTable[idx];
    numHashSearch++;
    while (head != NULL && (strcmp(name, head->name) != 0))
    {
        numHashSearch++;
        head = head->next;
    }
    return head;
}

void printType(struct Type *typePtr, int nrSpace)
{
    if (typePtr != NULL)
    {
        switch (typePtr->kind)
        {
        case BASIC:
        {
            if (typePtr->info.basicType == TYPE_INT)
            {
                printSpace(nrSpace);
                printf("Kind:BASIC(int)\n");
            }
            else
            {
                printSpace(nrSpace);
                printf("Kind:BASIC(float)\n");
            }
        }
        break;
        case ARRAY:
        {
            printSpace(nrSpace);
            printf("Kind:ARRAY, numElem = %d, elemWidth = %d\n", typePtr->info.array->numElem, typePtr->info.array->elemWidth);
            printType(typePtr->info.array->elem, nrSpace + 2);
        }
        break;
        case STRUCTURE:
        {
            printSpace(nrSpace);
            printf("Kind:STRUCTURE:%s\n", typePtr->info.structure->name);
            struct FieldList *ptr = typePtr->info.structure->structureInfo;
            while (ptr != NULL)
            {
                printSpace(nrSpace);
                printf("name=%s\n", ptr->name);
                printType(ptr->type, nrSpace + 2);
                ptr = ptr->next;
            }
        }
        break;
        default:
        {
            assert(0);
        }
        }
    }
    else
    {
        printSpace(nrSpace);
        printf("Type = NULL\n");
    }
}

struct NameNode **getNameHashSet(int sz)
{
    assert(sz > 0);
    struct NameNode **ptr = (struct NameNode **)malloc(sizeof(struct NameNode *) * sz);
    memset(ptr, 0, sizeof(struct NameNode *) * sz);
    return ptr;
}
int nameSetContains(struct NameNode **set, int sz, char *name)
{
    assert(name != NULL);
    int idx = hash(name, sz);
    struct NameNode *ptr = set[idx];
    while ((ptr != NULL) && (strcmp(ptr->name, name) != 0))
    {
        ptr = ptr->next;
    }
    return (ptr != NULL);
}
void insertIntoSet(struct NameNode **set, int sz, char *name)
{
    assert(name != NULL);
    int idx = hash(name, sz);
    struct NameNode *ptr = (struct NameNode *)malloc(sizeof(struct NameNode));
    //头部插入
    ptr->name = name;
    ptr->next = set[idx];
    set[idx] = ptr;
}
void freeSet(struct NameNode **set, int sz)
{
    for (int i = 0; i < sz; ++i)
    {
        struct NameNode *ptr = set[i];
        struct NameNode *pre = NULL;
        while (ptr != NULL)
        {
            pre = ptr;
            ptr = ptr->next;
            free(pre);
        }
    }
}

void printFieldList(struct FieldList *ptr, int nrSpace)
{
    while (ptr != NULL)
    {
        printSpace(nrSpace);
        printf("name:%s\n", ptr->name);
        printType(ptr->type, nrSpace);
        ptr = ptr->next;
    }
}

void handleProgram(struct TreeNode *r)
{
    assert(r->type == Node_Program);
    if ((r->numChildren == 1) && (r->children[0]->type == Node_ExtDefList))
    {
        handleExtDefList(r->children[0]);
    }
    else
    {
        assert(0);
    }
}

void handleExtDefList(struct TreeNode *r)
{
    assert(r->type == Node_ExtDefList);
    if (r->numChildren == 2)
    { //ExtDefLust -> ExtDef ExtDefList
        handleExtDef(r->children[0]);
        handleExtDefList(r->children[1]);
    }
    else if (r->numChildren != 0)
    {
        assert(0);
    }
}

void handleExtDef(struct TreeNode *r)
{
    assert(r->type == Node_ExtDef);
    struct Type *typePtr = handleSpecifier(r->children[0]);
    if ((r->numChildren == 3) && (r->children[2]->type == Node_SEMI))
    {
        //ExtDef -> Specifier ExtDecList SEMI
        struct FieldList *FL = handleExtDecList(r->children[1], typePtr); //传入类型，在内部创建变量
        // printFieldList(FL,0);
        //LAB3 没有全局变量
        assert(0);
    }
    else if ((r->numChildren == 3) && (r->children[2]->type == Node_Compst))
    {
        curSpace = 0; //清空
        //ExtDef -> Specifier FunDec Compst
        //也要为参数分配空间，在写函数的机器码的时候，要先从寄存器和栈(参数个数>4)中把参数复制到ebp以下
        struct Func *funcPtr = handleFuncDec(r->children[1], typePtr);
        //把函数的参数个数记录一下会更好
        int nrParams = 0;
        struct FieldList *params = funcPtr->params;
        while (params != NULL)
        {
            nrParams++;
            params = params->next;
        }
        funcPtr->nrParams = nrParams;
        //增加ICNode
        struct InterCode *ICFuncPtr = newICNode(1);
        ICFuncPtr->kind = IC_FUNC_DEF;
        ICFuncPtr->operands[0]->kind = OPERAND_FUNC;
        ICFuncPtr->operands[0]->info.funcName = funcPtr->name;
        appendInterCodeToList(ICFuncPtr);

        struct FieldList *paramListPtr = funcPtr->params;
        while (paramListPtr != NULL)
        {
            struct InterCode *ICPramPtr = newICNode(1);
            ICPramPtr->kind = IC_PARAM;
            //形式参数也是变量，有全局作用域
            ICPramPtr->operands[0]->kind = OPEARND_VAR;
            ICPramPtr->operands[0]->info.varName = paramListPtr->name;
            appendInterCodeToList(ICPramPtr);
            paramListPtr = paramListPtr->next;
        }

        //函数头加入函数表
        if (searchFuncTable(funcPtr->name) == NULL)
        {
            insertFuncTable(funcPtr);
        }
        else
        {
            //函数重定义
            printError(4, r->children[0]->line, "Function redefined.");
        }

        //打印函数体
        // printFuncDec(funcPtr,0);

        handleCompst(r->children[2], typePtr); //传入类型指针，遇到return判断返回值是否相容

        funcPtr->varSpace = curSpace;
        // fprintf(stderr, "function : %s, spaceSize = %d\n", funcPtr->name, curSpace);
    }
    else
    {
        //ExtDef -> Specifier Semi
        // printType(typePtr,0);
    }
}

struct Type *handleSpecifier(struct TreeNode *r)
{
    assert(r->type == Node_Sepcifier);
    struct Type *typePtr = NULL;
    if (r->children[0]->type == Node_TYPE)
    {
        //Specifier -> TYPE 基本类型
        typePtr = (struct Type *)malloc(sizeof(struct Type));
        typePtr->kind = BASIC;
        typePtr->info.basicType = r->children[0]->val.typeVal;
    }
    else
    {
        //Specifier -> StructSpecifier 结构类型
        typePtr = handleStructSpecifier(r->children[0]);
    }
    return typePtr;
}

struct FieldList *handleExtDecList(struct TreeNode *r, struct Type *typePtr)
{
    assert(r->type == Node_ExtDecList);
    if (r->numChildren == 1)
    {
        //ExtDecList -> VarDec
        struct FieldList *FL = handleVarDec2(r->children[0], typePtr);

        //反转数组
        if (FL->type->kind == ARRAY)
            reverseArray(FL->type);

        //插入变量表
        if ((searchVariableTable(FL->name) != NULL) || (searchStructureTable(FL->name) != NULL))
        {
            //错误3，变量名重复定义
            printError(3, r->children[0]->line, "Variable name used before.");
        }
        else
        {
            //可以定义这个变量
            struct Variable *varPtr = getVarPtr(FL, r->children[0]->line);
            insertVariableTable(varPtr);
        }
        return FL;
    }
    else
    {
        //ExtDecList -> VarDec comma ExtDecList
        struct FieldList *FL1 = handleVarDec2(r->children[0], typePtr); //只有一个节点

        //反转数组
        if (FL1->type->kind == ARRAY)
            reverseArray(FL1->type);

        if ((searchVariableTable(FL1->name) != NULL) || (searchStructureTable(FL1->name) != NULL))
        {
            //错误3，变量名重复定义
            printError(3, r->children[0]->line, "Variable name used before.");
        }
        else
        {
            //可以定义这个变量
            struct Variable *varPtr = getVarPtr(FL1, r->children[0]->line);
            insertVariableTable(varPtr);
        }
        //即使变量重定义了，还是存在于结果FL中
        struct FieldList *FL2 = handleExtDecList(r->children[2], typePtr); //>=0个节点
        FL1->next = FL2;
        return FL1;
    }
}

struct FieldList *handleVarDec2(struct TreeNode *r, struct Type *typePtr)
{
    assert(r->type == Node_VarDec);
    struct FieldList *FL = (struct FieldList *)malloc(sizeof(struct FieldList));
    if (r->numChildren == 1)
    {
        //VarDec -> ID
        FL->name = r->children[0]->idName;
        FL->next = NULL;
        FL->type = typePtr;
    }
    else
    {
        //VarDec -> VarDec LB INT RB
        //借助另一种VarDEc完成
        struct Type *typePtr2 = handleVarDec(r->children[0], typePtr, &(FL->name)); //namePtr会被修改
        struct Type *arrayPtr = (struct Type *)malloc(sizeof(struct Type));
        arrayPtr->kind = ARRAY;
        arrayPtr->info.array = (struct Array *)malloc(sizeof(struct Array));
        arrayPtr->info.array->elem = typePtr2;
        arrayPtr->info.array->numElem = r->children[2]->val.intVal;
        FL->next = NULL;
        FL->type = arrayPtr;
        //Attention!这里数组维度是逆序，int a[2][3]的3在Array链表头部
    }
    return FL;
}

struct Type *handleStructSpecifier(struct TreeNode *r)
{
    assert(r->type == Node_StructSpecifier);
    struct Type *typePtr = NULL;
    if (r->numChildren == 2)
    {
        //StructSpecifier -> Struct tag
        char *name = handleTag(r->children[1]);
        struct Structure *strucPtr = searchStructureTable(name);
        if (strucPtr == NULL)
        {
            //TODO报错,接着返回什么呢???NULL???我返回NULL了
            printError(17, r->children[1]->line, "Undefined structure");
        }
        else
        {
            //找到
            typePtr = (struct Type *)malloc(sizeof(struct Type));
            typePtr->kind = STRUCTURE;
            typePtr->info.structure = strucPtr;
        }
        return typePtr;
    }
    else
    {
        //StructSpecifier -> Struct OptTag Lc DefList RC
        char *name = handleOptTag(r->children[1]);
        //考虑结构体重名???
        if ((name != NULL) && ((searchStructureTable(name) != NULL) || (searchVariableTable(name) != NULL)))
        {
            //结构体和之前定义的结构体或者变量重名
            printError(16, r->children[1]->line, "Structure name used before");
            //返回什么类型呢?NULL 没有类型,如果后面跟着一个变量呢?他就没有类型吗?
            //所以还是要有类型的，不能直接返回NULL,这里只要报错就行了
            // return NULL;
        }
        //name有可能是NULL，即匿名结构，就不要加入结构体表
        struct FieldList *FL = handleDefList(r->children[3], 1); //struct里面的域不能初始化
        typePtr = (struct Type *)malloc(sizeof(struct Type));
        typePtr->kind = STRUCTURE;
        typePtr->info.structure = (struct Structure *)malloc(sizeof(struct Structure));
        typePtr->info.structure->structureInfo = FL;
        typePtr->info.structure->name = name; //名字是NULL也无所谓
        typePtr->info.structure->next = NULL;
        if (name != NULL)
        {
            insertStructureTable(typePtr->info.structure);
        }
        //判断结构体内部有没有重名的域,有就报错,即使报错，也不影响类型的正常返回
        checkNoDuplicateName(FL);
        return typePtr;
    }
}

char *handleTag(struct TreeNode *r)
{
    assert(r->type == Node_Tag);
    //Tag -> ID
    return r->children[0]->idName;
}

char *handleOptTag(struct TreeNode *r)
{
    assert(r->type == Node_OptTag);
    char *name = NULL;
    if (r->numChildren == 1)
    {
        name = r->children[0]->idName;
    }
    return name;
}

struct FieldList *handleDefList(struct TreeNode *r, int inStruc)
{
    assert(r->type == Node_DefList);
    if (r->numChildren == 0)
    {
        //DefLst -> epsilon
        return NULL;
    }
    else
    {
        //DefLst -> Def DefList
        struct FieldList *FL1 = handleDef(r->children[0], inStruc);     //数量>=1
        struct FieldList *FL2 = handleDefList(r->children[1], inStruc); //可能为空
        //找到FL1最后一个
        struct FieldList *tmp = FL1;
        while (tmp->next != NULL)
        {
            tmp = tmp->next;
        }
        tmp->next = FL2;
        return FL1;
    }
}

struct FieldList *handleDef(struct TreeNode *r, int inStruc)
{
    assert(r->type == Node_Def);
    //Def -> Specifier DecList Semi
    struct Type *typePtr = handleSpecifier(r->children[0]);
    return handleDecList(r->children[1], typePtr, inStruc);
}

struct FieldList *handleDecList(struct TreeNode *r, struct Type *typePtr, int inStruc)
{
    assert(r->type == Node_DecList);
    if (r->numChildren == 1)
    {
        //DecList -> Dec
        struct FieldList *FL = handleDec(r->children[0], typePtr, inStruc);
        return FL;
    }
    else
    {
        //DecList -> Dec comma Declist
        struct FieldList *FL1 = handleDec(r->children[0], typePtr, inStruc);
        struct FieldList *FL2 = handleDecList(r->children[2], typePtr, inStruc);
        FL1->next = FL2;
        return FL1;
    }
}

struct FieldList *handleDec(struct TreeNode *r, struct Type *typePtr, int inStruc)
{
    assert(r->type == Node_Dec);
    struct FieldList *FL = (struct FieldList *)malloc(sizeof(struct FieldList));
    if (r->numChildren == 1)
    {
        //Dec -> VarDec
        char *name = NULL;
        struct Type *typePtr2 = handleVarDec(r->children[0], typePtr, &name);

        //反转数组
        if (typePtr2->kind == ARRAY)
            reverseArray(typePtr2);

        //如果是数组，中间代码要分配空间,Lab3 不会出现结构体，结构体报错
        if (typePtr2->kind == STRUCTURE)
        {
            printf("Cannot translate: Code contains Variables or parameter of structure type.\n");
        }
        else if (typePtr2->kind == ARRAY)
        {
            //数组只能以这种不带初始化的方式创建，因为数组不能赋值
            int spaceNeeded = calculateWidth(typePtr2);
            struct InterCode *ICPtr = newICNode(1);
            ICPtr->kind = IC_DEC;
            ICPtr->allocSize = spaceNeeded;
            ICPtr->operands[0]->kind = OPEARND_VAR;
            ICPtr->operands[0]->info.varName = name;
            appendInterCodeToList(ICPtr);
        }

        FL->name = name;     //不空
        FL->type = typePtr2; //可能变成了数组
        FL->next = NULL;
        FL->line = r->children[0]->line;
    }
    else
    {
        //varDec = Exp
        if (inStruc)
        {
            //结构体内不能初始化
            printError(15, r->children[0]->line, "Cannot initialize field in structure.");
        }
        //就算不能初始化，如果出现语义错误，还是要报的
        //Dec -> VarDec Assign Exp
        char *name = NULL;
        struct Type *typePtr2 = handleVarDec(r->children[0], typePtr, &name);

        //反转数组
        if (typePtr2->kind == ARRAY)
            reverseArray(typePtr2);

        //如果是数组，中间代码要分配空间,Lab3 不会出现结构体，结构体报错
        if (typePtr2->kind == STRUCTURE)
        {
            printf("Cannot translate: Code contains Variables or parameter of structure type.\n");
        }
        else if (typePtr2->kind == ARRAY)
        {
            //这种情况不会发生，因为数组无法初始化
            // int spaceNeeded = calculateWidth(typePtr2);
            // struct InterCode * ICPtr = newICNode(1);
            // ICPtr->kind = IC_DEC;
            // ICPtr->allocSize = spaceNeeded;
            // ICPtr->operands[0]->kind = OPEARND_VAR;
            // ICPtr->operands[0]->info.varName = name;
            // appendInterCodeToList(ICPtr);
            assert(0);
        }

        //后面有赋值，只能是基本类型的初始化，要生成相应中间代码
        //先创建一个临时变量ti
        struct Operand *tmp = newOperand();
        //用Exp处理将tmp临时变量赋值，生成相应中间码
        struct Type *expTypePtr = handleExp(r->children[2], tmp, 1); //得到一个临时变量，如果是数组类型，要求取值
        struct InterCode *ICPtr = newICNode(1);                      //但是我需要2个，第二个自己初始化
        ICPtr->numOperands = 2;
        ICPtr->operands[1] = tmp;
        ICPtr->operands[0]->kind = OPEARND_VAR;
        ICPtr->operands[0]->info.varName = name; //name 不会是 NULL
        ICPtr->kind = IC_ASSIGN;
        appendInterCodeToList(ICPtr);

        FL->name = name;     //不空
        FL->type = typePtr2; //可能变成了数组
        FL->next = NULL;
        FL->line = r->children[0]->line;
        if (checkTypeSame(expTypePtr, typePtr2) == 0)
        {
            //赋值号两边表达式不匹配
            printError(5, r->children[1]->line, "Assignop mismatch.");
        }
    }

    //不在结构体中才插入变量表
    if (inStruc == 0)
    {
        if ((searchVariableTable(FL->name) == NULL) && (searchStructureTable(FL->name) == NULL))
        {
            //不存在重定义
            struct Variable *varPtr = getVarPtr(FL, FL->line);
            varPtr->isParam = 0; //局部变量
            insertVariableTable(varPtr);
        }
        else
        {
            //报错，变量重定义
            printError(3, FL->line, "Variable redefined.");
        }

        //记录函数内局部变量
        //将此变量和对于ebp的位置的偏移联系
        struct Variable *varPtr = searchVariableTable(FL->name);
        assert(varPtr != NULL);
        //增加curSpace
        if (FL->type->kind == BASIC)
        {
            varPtr->offsetToEbp = curSpace;
            curSpace += INT_FLOAT_SIZE;
        }
        else
        {
            assert(FL->type->kind == ARRAY);
            int sz = FL->type->info.array->elemWidth * FL->type->info.array->numElem;
            curSpace += sz;
            //由于栈是从高向低生长，因此把收个元素放在最低
            varPtr->offsetToEbp = curSpace - INT_FLOAT_SIZE;
        }
    }
    return FL;
}

struct Type *handleVarDec(struct TreeNode *r, struct Type *typePtr, char **namePtr)
{
    assert(r->type == Node_VarDec);
    if (r->numChildren == 1)
    {
        //VarDec -> ID
        *namePtr = r->children[0]->idName;
        return typePtr;
    }
    else
    {
        //VarDec -> VarDec LB INT RB
        struct Type *typePtr2 = handleVarDec(r->children[0], typePtr, namePtr); //namePtr会被修改
        struct Type *arrayPtr = (struct Type *)malloc(sizeof(struct Type));
        arrayPtr->kind = ARRAY;
        arrayPtr->info.array = (struct Array *)malloc(sizeof(struct Array));
        arrayPtr->info.array->elem = typePtr2;
        arrayPtr->info.array->numElem = r->children[2]->val.intVal;
        //Attention!这里数组维度是逆序，int a[2][3]的3在Array链表头部
        return arrayPtr;
    }
}

struct Variable *getVarPtr(struct FieldList *FL1, int line)
{
    struct Variable *varPtr = (struct Variable *)malloc(sizeof(struct Variable));
    varPtr->name = FL1->name;
    varPtr->firstAppearanceLine = line;
    varPtr->varType = FL1->type;
    varPtr->next = NULL;
}

struct Func *handleFuncDec(struct TreeNode *r, struct Type *typePtr)
{
    assert(r->type == Node_FuncDec);
    struct Func *funcPtr = (struct Func *)malloc(sizeof(struct Func));
    funcPtr->name = r->children[0]->idName; //ID
    funcPtr->next = NULL;
    funcPtr->retType = typePtr;
    if (r->numChildren == 3)
    {
        //Func -> Id LP RP
        funcPtr->params = NULL; //没有参数
    }
    else
    {
        //Func -> Id LP VarList RP
        funcPtr->params = handleVarList(r->children[2]);
    }
    return funcPtr;
}

struct FieldList *handleVarList(struct TreeNode *r)
{
    assert(r->type == Node_VarList);
    if (r->numChildren == 1)
    {
        //VarList -> ParamDec
        return handleParamDec(r->children[0]);
    }
    else
    {
        //VarList -> ParamDec comma ParamDec
        struct FieldList *FL1 = handleParamDec(r->children[0]);
        struct FieldList *FL2 = handleVarList(r->children[2]);
        FL1->next = FL2;
        return FL1;
    }
}

struct FieldList *handleParamDec(struct TreeNode *r)
{
    assert(r->type == Node_ParamDec);
    //ParamDec -> Specifier varDec
    struct Type *typePtr = handleSpecifier(r->children[0]);
    struct FieldList *FL = (struct FieldList *)malloc(sizeof(struct FieldList));
    FL->type = handleVarDec(r->children[1], typePtr, &(FL->name));

    //反转数组
    if (FL->type->kind == ARRAY)
        reverseArray(FL->type);

    FL->line = r->children[1]->line;
    FL->next = NULL;
    //检查重名参数，注意全局作用域
    if ((searchStructureTable(FL->name) != NULL) || (searchVariableTable(FL->name) != NULL))
    {
        //变量(形参)和变量、结构体重名
        printError(3, FL->line, "Variable redefined.");
    }
    else
    {
        struct Variable *varPtr = getVarPtr(FL, FL->line);
        varPtr->isParam = 1; //是形参
        //参数也放在栈中
        varPtr->offsetToEbp = curSpace;
        curSpace += INT_FLOAT_SIZE; //都是+4
        insertVariableTable(varPtr);
    }
    return FL;
}

void handleCompst(struct TreeNode *r, struct Type *typePtr)
{
    assert(r->type == Node_Compst);
    //Compst -> LC DefList StmtList RC
    struct FieldList *FL = handleDefList(r->children[1], 0); //可以带初始化
    handleStmtList(r->children[2], typePtr);                 //判断return语句是否正确
}

void handleStmtList(struct TreeNode *r, struct Type *typePtr)
{
    assert(r->type == Node_StmtList);
    if (r->numChildren > 0)
    {
        //StmtList -> Stmt StmtList
        handleStmt(r->children[0], typePtr);
        handleStmtList(r->children[1], typePtr);
    }
    //StmtList -> epsilon,don't care
}

void handleStmt(struct TreeNode *r, struct Type *typePtr)
{
    assert(r->type == Node_Stmt);
    if (r->numChildren == 1)
    {
        //Stmt -> Compst
        handleCompst(r->children[0], typePtr);
    }
    else if (r->numChildren == 2)
    {
        //Stmt -> Exp Semi
        //place不能为空的情况
        struct Operand *op = NULL;
        struct TreeNode *child = r->children[0];
        if (((child->numChildren >= 3) && (child->children[0]->type == Node_ID) && (child->children[1]->type == Node_LP)) //函数调用
            || (child->children[0]->type == Node_NOT)                                                                     //取反
            || ((child->numChildren == 3) && ((child->children[1]->type == Node_RELOP) || (child->children[1]->type == Node_AND) || (child->children[1]->type == Node_OR))))
        {
            op = newOperand();
        }
        handleExp(r->children[0], op, 0); //这个表达式的类型也不重要了
    }
    else if (r->numChildren == 3)
    {
        //Stmt -> Return Exp Semi
        //判断返回值类型

        struct Operand *opRet = newOperand();
        //函数返回值不一定是TMP_VAR，如果返回值是普通变量或者常数，就不是
        struct Type *expTypePtr = handleExp(r->children[1], opRet, 1);
        //如果Exp返回类型为NULL,说明Exp里面已经报错，不用再报
        if ((expTypePtr != NULL) && checkTypeSame(expTypePtr, typePtr) == 0)
        {
            printError(8, r->children[0]->line, "Return type mismatch.");
        }
        // assert(opRet->kind == OPEARND_TMP_VAR);
        // return也可能是三种:常量、临时变量、变量
        struct InterCode *ICPtr = newICNode(-1);
        ICPtr->numOperands = 1;
        ICPtr->operands[0] = opRet;
        ICPtr->kind = IC_RETURN;
        appendInterCodeToList(ICPtr);
    }
    else if (r->numChildren == 5)
    {
        if (r->children[0]->type == Node_IF)
        {
            //Stmt -> If LP Exp RP Stmt
            //OLD LAB2 CODE
            // struct Type * expTypePtr = handleExp(r->children[2], NULL,  0);
            // handleStmt(r->children[4],typePtr);//返回值留给Stmt判断
            //NEW LAB3 code
            struct Operand *L1 = getNewLabel();
            struct Operand *L2 = getNewLabel();
            struct InterCode *ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L1;

            translateCond(r->children[2], L1, L2); //code1

            appendInterCodeToList(ICPtr); //L1

            handleStmt(r->children[4], typePtr); //code2
            ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L2;
            appendInterCodeToList(ICPtr); //L2
        }
        else
        {
            //和IF 一样的......
            //Stmt -> while LP Exp RP Stmt
            // struct Type * expTypePtr = handleExp(r->children[2], NULL,  0);
            // //TODO 判断exp的类型是不是INT,但是老师说不考虑，不管
            // handleStmt(r->children[4],typePtr);//返回值留给Stmt判断

            //NEW code
            struct Operand *L1 = getNewLabel();
            struct Operand *L2 = getNewLabel();
            struct Operand *L3 = getNewLabel();
            struct InterCode *ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L1;

            appendInterCodeToList(ICPtr); //L1

            translateCond(r->children[2], L2, L3); //code1

            ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L2;
            appendInterCodeToList(ICPtr); //L2

            handleStmt(r->children[4], typePtr); //code2

            ICPtr = newICNode(-1);
            ICPtr->kind = IC_GOTO;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L1;
            appendInterCodeToList(ICPtr); //GOTO L1 再次判断

            ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L3;
            appendInterCodeToList(ICPtr); //L3
        }
    }
    else
    {
        //Stmt -> If LP Exp RP Stmt else Stmt
        // struct Type * expTypePtr = handleExp(r->children[2], NULL,  0);
        // //TODO 判断exp的类型是不是INT,但是老师说不考虑，不管
        // handleStmt(r->children[4],typePtr);//返回值留给Stmt判断
        // handleStmt(r->children[6],typePtr);//返回值留给Stmt判断

        //NEW code
        struct Operand *L1 = getNewLabel();
        struct Operand *L2 = getNewLabel();
        struct Operand *L3 = getNewLabel();
        struct InterCode *ICPtr = newICNode(-1);
        ICPtr->kind = IC_LABEL_DEF;
        ICPtr->numOperands = 1;
        ICPtr->operands[0] = L1;

        translateCond(r->children[2], L1, L2); //code1

        appendInterCodeToList(ICPtr); //L1

        handleStmt(r->children[4], typePtr); //code2

        ICPtr = newICNode(-1);
        ICPtr->kind = IC_GOTO;
        ICPtr->numOperands = 1;
        ICPtr->operands[0] = L3;
        appendInterCodeToList(ICPtr); //GOTO L3

        ICPtr = newICNode(-1);
        ICPtr->kind = IC_LABEL_DEF;
        ICPtr->numOperands = 1;
        ICPtr->operands[0] = L2;
        appendInterCodeToList(ICPtr); //L2

        handleStmt(r->children[6], typePtr);

        ICPtr = newICNode(-1);
        ICPtr->kind = IC_LABEL_DEF;
        ICPtr->numOperands = 1;
        ICPtr->operands[0] = L3;
        appendInterCodeToList(ICPtr); //L3
    }
}

struct Type *handleExp(struct TreeNode *r, struct Operand *place, int needGetValue)
{
    //place是使用newOperand创建的操作数，然而操作数的类型的值由本函数确定S
    assert(r->type == Node_Exp);
    if (r->numChildren == 1)
    {
        if (r->children[0]->type == Node_ID)
        {
            //Exp -> ID
            //查找变量表
            struct Variable *varPtr = searchVariableTable(r->children[0]->idName);
            if (varPtr == NULL)
            {
                //没有这个变量
                printError(1, r->children[0]->line, "Variable Undefined.");
                return NULL; //这里的NULL实在没办法忽视了，真的没有类型
            }
            else
            {
                if (place != NULL)
                {
                    //ID可能是基本类型或者基本类型数组(不会出现结构)
                    //place的名字已经确定了
                    if (varPtr->varType->kind == BASIC)
                    {
                        //place就是VAR，即EXP的值就是VAR
                        place->kind = OPEARND_VAR;
                        place->info.varName = varPtr->name;
                    }
                    else if (varPtr->varType->kind == ARRAY)
                    {
                        //数组的话，ID应该已经在中间代码DEC过了。
                        struct InterCode *ICPtr = newICNode(-1);
                        if (searchVariableTable(varPtr->name)->isParam == 0)
                        {
                            //局部数组要取地址
                            ICPtr->numOperands = 2;
                            ICPtr->operands[0] = place;
                            place->kind == OPEARND_ADDR;
                            place->info.tmpVarNo = getNextTmpNo();
                            ICPtr->operands[1] = newOperand();
                            ICPtr->operands[1]->kind = OPEARND_VAR;
                            ICPtr->operands[1]->info.varName = varPtr->name;
                            place->kind = OPEARND_TMP_VAR; //使用一个临时变量来存储数组地址
                            ICPtr->kind = IC_GET_ADDR;     //place := & x
                            appendInterCodeToList(ICPtr);
                        }
                        else
                        {
                            //数组是一个参数，因此数组名变量就是首地址
                            place->kind = OPEARND_VAR;
                            place->info.varName = varPtr->name;
                        }
                    }
                    else
                    {
                        assert(0);
                    }
                }
                return varPtr->varType;
            }
        }
        else if (r->children[0]->type == Node_INT)
        {
            //Exp -> INT
            //创建一个Type*
            struct Type *typePtr = (struct Type *)malloc(sizeof(struct Type));
            typePtr->kind = BASIC;
            typePtr->info.basicType = TYPE_INT;

            if (place != NULL)
            {
                //EXP是一个常数，直接设置place的值就行
                place->kind = OPEARND_CONSTANT;
                place->info.constantVal = r->children[0]->val.intVal;
            }

            return typePtr;
        }
        else
        {
            //Exp -> FLAOT
            //创建一个Type*
            printf("float is not allowed in Lab3.\n");
            assert(0); //lab3没有float
            struct Type *typePtr = (struct Type *)malloc(sizeof(struct Type));
            typePtr->kind = BASIC;
            typePtr->info.basicType = TYPE_FLOAT;
            return typePtr;
        }
    }
    else if (r->numChildren == 2)
    {
        if (r->children[0]->type == Node_NOT)
        {
            //Exp -> Not Exp
            assert(place != NULL);
            struct Operand *L1 = getNewLabel();
            struct Operand *L2 = getNewLabel();
            struct Operand *const0 = newOperand();
            const0->kind = OPEARND_CONSTANT;
            const0->info.constantVal = 0;
            struct InterCode *ICPtr = newICNode(-1);
            ICPtr->numOperands = 2;
            ICPtr->operands[0] = place;
            place->kind = OPEARND_TMP_VAR;
            place->info.tmpVarNo = getNextTmpNo();
            ICPtr->operands[1] = const0;
            ICPtr->kind = IC_ASSIGN;
            appendInterCodeToList(ICPtr);

            //中间是Cond翻译
            struct Type *typePtr = translateCond(r, L1, L2);

            ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L1;
            appendInterCodeToList(ICPtr);

            struct Operand *const1 = newOperand();
            const1->kind = OPEARND_CONSTANT;
            const1->info.constantVal = 1;
            ICPtr = newICNode(-1);
            ICPtr->numOperands = 2;
            ICPtr->operands[0] = place;
            ICPtr->operands[1] = const1;
            ICPtr->kind = IC_ASSIGN;
            appendInterCodeToList(ICPtr);

            ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L2;
            appendInterCodeToList(ICPtr);

            //判断Exp 是不是NULL或者basic类型
            // struct Type * typePtr = handleExp(r->children[1], NULL,  0);//有可能得到NULL
            if ((typePtr == NULL) || (typePtr->kind != BASIC))
            {
                //操作数类型不匹配,Not只能跟基本类型INT或者FLoat(我用C语言试了)
                printError(7, r->children[0]->line, "Variable(s) type mismatch.");
                return NULL; //这里返回????没得返回
            }
            else
            {
                return typePtr; //类型不变
            }
        }
        else
        {
            //Exp -> MINUS Exp
            //判断Exp 是不是NULL或者basic类型
            //t1=0，不是临时变量，就是常量
            struct Operand *t1 = newOperand();
            t1->kind = OPEARND_CONSTANT;
            t1->info.constantVal = 0;
            struct Operand *t2 = newOperand();
            struct Type *typePtr = handleExp(r->children[1], t2, 1);
            if ((typePtr == NULL) || (typePtr->kind != BASIC))
            {
                //操作数类型不匹配
                printError(7, r->children[0]->line, "Variable(s) type mismatch.");
                return NULL; //????
            }
            else
            {
                assert(t1->kind == OPEARND_CONSTANT);
                if (place != NULL)
                {
                    struct InterCode *INPtr = newICNode(-1);
                    INPtr->numOperands = 3;
                    INPtr->operands[0] = place;
                    place->kind = OPEARND_TMP_VAR;
                    place->info.tmpVarNo = getNextTmpNo();
                    INPtr->operands[1] = t1;
                    INPtr->operands[2] = t2;
                    INPtr->kind = IC_SUB;
                    appendInterCodeToList(INPtr);
                }
                return typePtr; //类型不变
            }
        }
    }
    else if (r->numChildren == 3)
    {
        if (r->children[1]->type == Node_ASSIGNOP)
        {
            //赋值号左边只能是左值，仅从语法上检查,Exp只能是ID | Exp LB EXP RB | Exp Dot ID
            if (((r->children[0]->numChildren == 1) && (r->children[0]->children[0]->type == Node_ID)) || ((r->children[0]->numChildren == 4) && (r->children[0]->children[1]->type == Node_LB)) || ((r->children[0]->numChildren == 3) && (r->children[0]->children[1]->type == Node_DOT)))
            {
                //什么都不做(^-^)
            }
            else
            {
                printError(6, r->children[1]->line, "The left-hand side of an assignment must be a variable.");
                //报错之后，还要分析吗?????
                //暂时继续分析吧TODO may FIX it
            }
            //Exp -> Exp Assign Exp
            struct Operand *left = newOperand();
            struct Operand *right = newOperand();

            struct Type *typePtr1 = handleExp(r->children[0], left, 0); //如果是数组需要不需要取值
            struct Type *typePtr2 = handleExp(r->children[2], right, 1);
            //这两个都有可能返回NULL,如果有一个NULL，说明其中一个Exp的类型不存在(出错)
            if ((typePtr1 == NULL) || (typePtr2 == NULL))
            {
                //如果有一个是NULL,说明之前已经错了，这里就不报错了
                assert(0); //这种情况再lab3不会发生
                return NULL;
            }
            if (checkTypeSame(typePtr1, typePtr2) == 1)
            { //类型相同，非空
                //生成赋值语句中间代码
                //这里有可能是数组赋值
                if (typePtr1->kind == ARRAY)
                {
                    //实际上不会发生
                    // assert(0);
                    int w1 = typePtr1->info.array->elemWidth;
                    int n1 = typePtr1->info.array->numElem;
                    int w2 = typePtr2->info.array->elemWidth;
                    int n2 = typePtr2->info.array->numElem;
                    int copySz = min(w1 * n1, w2 * n2);
                    //逐元素复制
                    //left和right都是Addr类型。有可能是临时变量，也可能是数组变量
                    //都先存到一个临时变量t1,t2中，之后每次递增t1,t2 = t1,t2 + 4
                    struct Operand *t1 = newOperand();
                    t1->kind = OPEARND_ADDR;
                    t1->info.tmpVarNo = getNextTmpNo();
                    struct Operand *t2 = newOperand();
                    t2->kind = OPEARND_ADDR;
                    t2->info.tmpVarNo = getNextTmpNo();

                    //t1 := left
                    struct InterCode *ICPtr = newICNode(-1);
                    ICPtr->kind = IC_ASSIGN;
                    ICPtr->numOperands = 2;
                    ICPtr->operands[0] = t1;
                    ICPtr->operands[1] = left;
                    appendInterCodeToList(ICPtr);

                    //t2 := right
                    ICPtr = newICNode(-1);
                    ICPtr->kind = IC_ASSIGN;
                    ICPtr->numOperands = 2;
                    ICPtr->operands[0] = t2;
                    ICPtr->operands[1] = right;
                    appendInterCodeToList(ICPtr);

                    struct Operand *const4 = newOperand();
                    const4->kind = OPEARND_CONSTANT;
                    const4->info.constantVal = INT_FLOAT_SIZE;

                    //临时变量存右侧地址取值的结果
                    struct Operand *tmp = newOperand();
                    tmp->kind = OPEARND_TMP_VAR;
                    tmp->info.constantVal = getNextTmpNo();

                    for (int i = 0; i < copySz / INT_FLOAT_SIZE; ++i)
                    {
                        //取右侧元素值
                        ICPtr = newICNode(-1);
                        ICPtr->numOperands = 2;
                        ICPtr->operands[0] = tmp;
                        ICPtr->operands[1] = t2;
                        ICPtr->kind = IC_GET_VALUE;
                        appendInterCodeToList(ICPtr);

                        //tmp复制给左侧
                        ICPtr = newICNode(-1);
                        ICPtr->numOperands = 2;
                        ICPtr->operands[0] = t1;
                        ICPtr->operands[1] = tmp;
                        ICPtr->kind = IC_WRITE_TO_ADDR;
                        appendInterCodeToList(ICPtr);

                        //t1,t2 += 4
                        if (i != copySz / INT_FLOAT_SIZE - 1)
                        {
                            ICPtr = newICNode(-1);
                            ICPtr->kind = IC_PLUS;
                            ICPtr->numOperands = 3;
                            ICPtr->operands[0] = t1;
                            ICPtr->operands[1] = t1;
                            ICPtr->operands[2] = const4;
                            appendInterCodeToList(ICPtr);

                            ICPtr = newICNode(-1);
                            ICPtr->kind = IC_PLUS;
                            ICPtr->numOperands = 3;
                            ICPtr->operands[0] = t2;
                            ICPtr->operands[1] = t2;
                            ICPtr->operands[2] = const4;
                            appendInterCodeToList(ICPtr);
                        }
                    }
                    if (place != NULL)
                    {
                        //把赋值号左边的地址放在place中
                        place->kind = OPEARND_TMP_VAR; //将EXP的结果放在一个临时变量中
                        place->info.tmpVarNo = getNextTmpNo();
                        ICPtr = newICNode(-1);
                        ICPtr->numOperands = 2;
                        ICPtr->operands[0] = place;
                        ICPtr->operands[1] = left;
                        ICPtr->kind = IC_ASSIGN;
                        appendInterCodeToList(ICPtr);
                    }
                }
                else
                {
                    struct InterCode *ICPtr = newICNode(-1);
                    ICPtr->numOperands = 2;
                    ICPtr->operands[0] = left;
                    ICPtr->operands[1] = right;
                    //right的类型可能是VAR、TMP_VAR和CONST
                    if (left->kind == OPEARND_ADDR)
                    { //是地址
                        //*x := y
                        ICPtr->kind = IC_WRITE_TO_ADDR;
                    }
                    else
                    {
                        //赋值号左侧是一个普通变量,直接赋值给变量
                        ICPtr->kind = IC_ASSIGN;
                    }
                    appendInterCodeToList(ICPtr);
                    if (place != NULL)
                    {
                        //给place赋值,这下=表达式的结果一定存在临时变量place中
                        //对于stmt中的 EXp = ...语句，place是NULL，因为不需要取得EXP的值
                        place->kind = OPEARND_TMP_VAR; //将EXP的结果放在一个临时变量中
                        place->info.tmpVarNo = getNextTmpNo();
                        ICPtr = newICNode(-1);
                        ICPtr->numOperands = 2;
                        ICPtr->operands[0] = place;
                        ICPtr->operands[1] = right;
                        ICPtr->kind = IC_ASSIGN;
                        appendInterCodeToList(ICPtr);
                    }
                }
                return typePtr1;
            }
            else
            {
                //报错,赋值号类型不匹配
                printError(5, r->children[1]->line, "Assignop mismatch.");
                return NULL; //不能赋值就返回NULL
            }
        }
        else if ((r->children[1]->type == Node_AND) || (r->children[1]->type == Node_OR) || (r->children[1]->type == Node_RELOP))
        {
            //Exp -> Exp AND/OR/RELOP  Exp
            assert(place != NULL);
            struct Operand *L1 = getNewLabel();
            struct Operand *L2 = getNewLabel();
            struct Operand *const0 = newOperand();
            const0->kind = OPEARND_CONSTANT;
            const0->info.constantVal = 0;
            struct InterCode *ICPtr = newICNode(-1);
            ICPtr->numOperands = 2;
            place->kind = OPEARND_TMP_VAR;
            place->info.tmpVarNo = getNextTmpNo();
            ICPtr->operands[0] = place;
            ICPtr->operands[1] = const0;
            ICPtr->kind = IC_ASSIGN;
            appendInterCodeToList(ICPtr);

            //中间是Cond翻译
            struct Type *curType = translateCond(r, L1, L2);

            ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L1;
            appendInterCodeToList(ICPtr);

            struct Operand *const1 = newOperand();
            const1->kind = OPEARND_CONSTANT;
            const1->info.constantVal = 1;
            ICPtr = newICNode(-1);
            ICPtr->numOperands = 2;
            ICPtr->operands[0] = place;
            ICPtr->operands[1] = const1;
            ICPtr->kind = IC_ASSIGN;
            appendInterCodeToList(ICPtr);

            ICPtr = newICNode(-1);
            ICPtr->kind = IC_LABEL_DEF;
            ICPtr->numOperands = 1;
            ICPtr->operands[0] = L2;
            appendInterCodeToList(ICPtr);

            return curType;
        }
        else if ((r->children[1]->type == Node_PLUS) || (r->children[1]->type == Node_MINUS) || (r->children[1]->type == Node_STAR) || (r->children[1]->type == Node_DIV))
        {
            //Exp -> Exp PLUS/MINUS... Exp
            struct Operand *t1 = newOperand();
            struct Operand *t2 = newOperand();
            //t1 和 t2的具体内容由handleExp确定
            //如果是数组元素，需要从地址去除值。因此第三个参数 = 1
            struct Type *typePtr1 = handleExp(r->children[0], t1, 1);
            struct Type *typePtr2 = handleExp(r->children[2], t2, 1);
            if ((typePtr1 == NULL) || (typePtr2 == NULL))
            {
                //如果有一个是NULL,说明之前已经错了，这里就不报错了
                return NULL;
            }
            //基本类型才能PLUS
            if (checkTypeSame(typePtr1, typePtr2) && ((typePtr1->kind == BASIC)))
            {
                //不一定是临时变量，也可能是VAR或者CONST
                // assert(t1->kind == OPEARND_TMP_VAR &&t2->kind == OPEARND_TMP_VAR);
                if (place != NULL)
                {
                    struct InterCode *INPtr = newICNode(-1);
                    INPtr->numOperands = 3;
                    INPtr->operands[0] = place;
                    place->kind = OPEARND_TMP_VAR; //存到临时变量
                    place->info.tmpVarNo = getNextTmpNo();
                    INPtr->operands[1] = t1;
                    INPtr->operands[2] = t2;
                    switch (r->children[1]->type)
                    {
                    case Node_PLUS:
                    {
                        INPtr->kind = IC_PLUS;
                    }
                    break;
                    case Node_MINUS:
                    {
                        INPtr->kind = IC_SUB;
                    }
                    break;
                    case Node_STAR:
                    {
                        INPtr->kind = IC_MUL;
                    }
                    break;
                    case Node_DIV:
                    {
                        INPtr->kind = IC_DIV;
                    }
                    break;
                    default:
                    {
                        assert(0);
                    }
                    break;
                    }
                    appendInterCodeToList(INPtr);
                }
                return typePtr1;
            }
            else
            {
                printError(7, r->children[1]->line, "Oprand(s) or operator mismatch.");
                return NULL; //不能赋值就返回NULL
            }
        }
        else if (r->children[0]->type == Node_LP)
        {
            //Exp -> LP Exp RP
            return handleExp(r->children[1], place, needGetValue);
        }
        else if (r->children[1]->type == Node_LP)
        {
            //Exp -> ID LP RP
            if (strcmp(r->children[0]->idName, "read") == 0)
            {
                //读取一个值先放到临时变量place中，再做其他处理
                // assert(place->kind == OPEARND_TMP_VAR);
                struct InterCode *ICPtr = newICNode(-1);
                place->kind = OPEARND_TMP_VAR;
                place->info.tmpVarNo = getNextTmpNo();
                ICPtr->numOperands = 1;
                ICPtr->operands[0] = place;
                ICPtr->kind = IC_READ;
                appendInterCodeToList(ICPtr);
                //返回的类型是INT
                return intTypePtr;
            }
            //函数调用
            struct Func *funcPtr = searchFuncTable(r->children[0]->idName);
            if (funcPtr == NULL)
            {
                if (searchVariableTable(r->children[0]->idName) != NULL)
                {
                    //对普通变量使用()
                    printError(11, r->children[0]->line, "Try use () to normal variable.");
                }
                else //调用未定义的函数
                    printError(2, r->children[0]->line, "Function Undefined.");
                return NULL;
            }
            else
            {
                //函数定义了，判断参数是否符合
                if (checkFieldListSame(funcPtr->params, NULL))
                {
                    //CALL
                    if (place != NULL)
                    {
                        struct InterCode *INPtr = newICNode(-1);
                        INPtr->numOperands = 2;
                        INPtr->operands[0] = place;
                        //返回值放在临时变量中
                        place->kind = OPEARND_TMP_VAR;
                        place->info.tmpVarNo = getNextTmpNo();
                        struct Operand *op = newOperand();
                        op->kind = OPERAND_FUNC;
                        op->info.funcName = r->children[0]->idName;
                        INPtr->operands[1] = op;
                        INPtr->kind = IC_CALL;
                        appendInterCodeToList(INPtr);
                    }
                    return funcPtr->retType; //返回的是函数返回类型
                }
                else
                {
                    printError(9, r->children[0]->line, "Function call parameters mismatch.");
                    return NULL;
                }
            }
        }
        else
        {
            //Exp -> Exp DOT ID
            assert(r->children[1]->type == Node_DOT); //前面太多了，assert一下
            struct Type *expTypePtr = handleExp(r->children[0], NULL, 0);
            //判断ptr是否非NULL且为Structure并且有域ID
            if (expTypePtr == NULL)
            {
                //如果是NULL,说明递归exp已经错了，这里就不报错了
                return NULL;
            }
            if ((expTypePtr != NULL) && (expTypePtr->kind == STRUCTURE))
            {
                struct FieldList *FL = expTypePtr->info.structure->structureInfo; //结构体的域
                while ((FL != NULL) && (strcmp(FL->name, r->children[2]->idName) != 0))
                {
                    FL = FL->next;
                }
                if (FL == NULL)
                {
                    //没有这个域14
                    printError(14, r->children[1]->line, "Access undefined field in structure.");
                    return NULL;
                }
                else
                {
                    return FL->type;
                }
            }
            else
            {
                //Exp的类型不是结构体
                //对非结构体使用.操作
                printError(13, r->children[1]->line, "Use '.' to non-Strcuture.");
                return NULL;
            }
        }
    }
    else
    {
        //num == 4
        assert(r->numChildren == 4);
        if (r->children[0]->type == Node_ID)
        {
            //Exp -> ID LP Args RP
            struct Func *funcPtr = searchFuncTable(r->children[0]->idName);
            if (funcPtr == NULL)
            {
                if (searchVariableTable(r->children[0]->idName) != NULL)
                {
                    //对普通变量使用()
                    printError(11, r->children[0]->line, "Try use () to normal variable.");
                }
                else //调用未定义的函数
                    printError(2, r->children[0]->line, "Function Undefined.");
                return NULL;
            }
            else
            {
                //函数定义了，判断参数是否符合
                //先得到Args:FL,只有type重要,name不需要
                //对于LAB#，要将args里面的每一个exp都计算出来放到临时变量中。注意，arg可以有地址
                //对于数组，只能传递一维数组，用argList链表表示。注意逆序。ARG中间代码不是再hadleArgs的过程中生成
                struct ArgNode *argList = NULL;
                struct FieldList *FL = handleArgs(r->children[2], &argList);
                if (checkFieldListSame(funcPtr->params, FL))
                {
                    if (strcmp(r->children[0]->idName, "write") == 0)
                    {
                        //把第一个参数exp的值放到一个局部变量
                        assert((argList != NULL) && (argList->op != NULL));
                        struct Operand *tmp = argList->op;
                        //write的值可能是变量、常量 、临时变量。因为EXp可能把place设置为这些
                        // assert(tmp->kind == OPEARND_TMP_VAR);
                        struct InterCode *ICPtr = newICNode(-1);
                        ICPtr->numOperands = 1;
                        ICPtr->operands[0] = argList->op;
                        ICPtr->kind = IC_WRITE;
                        appendInterCodeToList(ICPtr);
                    }
                    else
                    {
                        //普通函数，把argList中的一个个顺序加入ICList，最后place : call f
                        //对于每个实参，有两种可能
                        //1. exp是一个值，存在了临时变量op中
                        //2. exp是一个一维数组，还有两种可能
                        //(1). 这个数组是局部变量，需要进行&操作得到数组首地址放到临时变量中再ARG
                        //(2). 这个数组是形参传下来的，EXP解析的时候，类型也是ARRAY。要检查是哪一种，如果是形参，就直接把形参对应的变量名作为这里的实参
                        //但是以上都由printICPtr处理
                        assert(argList != NULL);
                        struct ArgNode *curArg = argList;
                        struct InterCode *INPtr = NULL;
                        do
                        {
                            INPtr = newICNode(-1);
                            INPtr->kind = IC_ARG;
                            INPtr->numOperands = 1;
                            INPtr->operands[0] = curArg->op;
                            appendInterCodeToList(INPtr);
                            curArg = curArg->next;
                        } while (curArg != argList);
                        //还有一个call
                        if (place != NULL)
                        {
                            INPtr = newICNode(-1);
                            INPtr->numOperands = 2;
                            INPtr->operands[0] = place;
                            //返回值放在临时变量中
                            place->kind = OPEARND_TMP_VAR;
                            place->info.tmpVarNo = getNextTmpNo();
                            struct Operand *op = newOperand();
                            op->kind = OPERAND_FUNC;
                            op->info.funcName = r->children[0]->idName;
                            INPtr->operands[1] = op;
                            INPtr->kind = IC_CALL;
                            appendInterCodeToList(INPtr);
                        }
                    }
                    return funcPtr->retType; //返回的是函数返回类型
                }
                else
                {
                    printError(9, r->children[0]->line, "Function call parameters mismatch.");
                    return NULL;
                }
            }
        }
        else
        {
            //Exp -> Exp LB Exp RB
            assert(r->children[0]->type == Node_Exp);

            struct Operand *tmpIdx = newOperand();
            struct Operand *preOffset = newOperand();
            struct Type *typePtr1 = handleExp((r->children[0]), preOffset, 0); //将preOffset设置好
            struct Type *typePtr2 = handleExp((r->children[2]), tmpIdx, 1);    //得到下标值
            //preOffset有多种可能: 参数数组ID,是VAR。 局部数组地址，是ADDR
            //tmpIdx也有多种可能：CONST或者TMP_VAR或者VAR
            //1应该是array,2是INT
            if ((typePtr1 != NULL) && (typePtr1->kind == ARRAY) && (typePtr2 != NULL) && (typePtr2->kind == BASIC) && (typePtr2->info.basicType == TYPE_INT))
            {
                //要新建一个临时变量计算新的偏移量
                //先计算要加上的偏移量，用idx*width得到
                if (place != NULL)
                {
                    struct Operand *addOffset = newOperand();
                    addOffset->kind = OPEARND_TMP_VAR;
                    addOffset->info.tmpVarNo = getNextTmpNo();
                    struct Operand *width = newOperand();
                    width->kind = OPEARND_CONSTANT;
                    width->info.constantVal = typePtr1->info.array->elemWidth;
                    struct InterCode *ICPtr = newICNode(-1);
                    ICPtr->numOperands = 3;
                    ICPtr->operands[0] = addOffset;
                    ICPtr->operands[1] = tmpIdx;
                    ICPtr->operands[2] = width;
                    ICPtr->kind = IC_MUL;
                    appendInterCodeToList(ICPtr);
                    //现在addoffest对应的临时变量已经记录的新加偏移量了
                    //得到加和总偏移量
                    struct Operand *sumOffset = newOperand(); //产生一个新的临时变量
                    sumOffset->kind = OPEARND_ADDR;
                    sumOffset->info.tmpVarNo = getNextTmpNo();
                    ICPtr = newICNode(-1);
                    ICPtr->numOperands = 3;
                    ICPtr->operands[0] = sumOffset;
                    ICPtr->operands[1] = preOffset; //可能是多种类型
                    ICPtr->operands[2] = addOffset;
                    ICPtr->kind = IC_PLUS;
                    appendInterCodeToList(ICPtr);

                    if (typePtr1->info.array->elem->kind != ARRAY)
                    {
                        //当前是最后一个，smuOffet已经是完全地址了
                        if (needGetValue == 0)
                        {
                            //绝对地址就是sumOffset对应的临时变量
                            place->kind = OPEARND_ADDR;
                            place->info.tmpVarNo = sumOffset->info.tmpVarNo;
                        }
                        else
                        {
                            //取地址上的数值
                            ICPtr = newICNode(-1);
                            ICPtr->numOperands = 2;
                            place->kind = OPEARND_TMP_VAR;
                            place->info.tmpVarNo = getNextTmpNo();
                            //元素值存在一个临时变量中
                            ICPtr->operands[0] = place;
                            ICPtr->operands[1] = sumOffset;
                            ICPtr->kind = IC_GET_VALUE;
                            appendInterCodeToList(ICPtr);
                        }
                    }
                    else
                    {
                        //元素寻址过程未结束
                        place->kind = OPEARND_ADDR;
                        place->info.tmpVarNo = sumOffset->info.tmpVarNo;
                    }
                }

                return typePtr1->info.array->elem; //返回上一层类型
            }
            else
            {
                if (!((typePtr1 != NULL) && (typePtr1->kind == ARRAY)))
                {
                    //非数组使用[]
                    printError(10, r->children[1]->line, "Use [] to no-Array.");
                    return NULL;
                }
                else
                {
                    //[]非整数,也只是内部出错，还是返回子类型,不影响后续分析
                    printError(12, r->children[1]->line, "no-Interger in [].");
                    return typePtr1->info.array->elem;
                }
                // return NULL;//没办法，只能NULL
            }
        }
    }
}

void setRelop(struct InterCode *ICPtr, char *relop)
{
    if (strcmp(relop, "<") == 0)
    {
        ICPtr->relop = LT;
    }
    else if (strcmp(relop, "<=") == 0)
    {
        ICPtr->relop = LEQ;
    }
    else if (strcmp(relop, ">") == 0)
    {
        ICPtr->relop = GT;
    }
    else if (strcmp(relop, ">=") == 0)
    {
        ICPtr->relop = GEQ;
    }
    else if (strcmp(relop, "!=") == 0)
    {
        ICPtr->relop = NEQ;
    }
    else if (strcmp(relop, "==") == 0)
    {
        ICPtr->relop = EQ;
    }
    else
    {
        assert(0);
    }
}

struct Type *translateCond(struct TreeNode *r, struct Operand *labelTrue, struct Operand *labelFalse)
{
    //仍然要返回表达式类型
    //r : Exp
    assert(r->type == Node_Exp);

    if ((r->numChildren == 3) && (r->children[1]->type == Node_RELOP))
    {
        struct Operand *t1 = newOperand();
        struct Operand *t2 = newOperand();
        //计算两个表达式的值到操作数t1和t2中
        struct Type *typePtr1 = handleExp(r->children[0], t1, 1); //code1
        struct Type *typePtr2 = handleExp(r->children[2], t2, 1); //code2
        assert(checkTypeSame(typePtr1, typePtr2) == 1);
        //relop有6种
        struct InterCode *ICPtr = newICNode(-1);
        ICPtr->kind = IC_RELOP_GOTO;
        ICPtr->numOperands = 3;
        ICPtr->operands[0] = t1;
        ICPtr->operands[1] = t2;
        ICPtr->operands[2] = labelTrue;
        setRelop(ICPtr, r->children[1]->idName);
        appendInterCodeToList(ICPtr); //code3

        //无条件跳转到false
        ICPtr = newICNode(-1);
        ICPtr->kind = IC_GOTO;
        ICPtr->numOperands = 1;
        ICPtr->operands[0] = labelFalse;
        appendInterCodeToList(ICPtr);
        return typePtr1;
    }
    else if ((r->numChildren == 3) && ((r->children[1]->type == Node_AND) || (r->children[1]->type == Node_OR)))
    {
        struct Operand *L1 = getNewLabel();
        struct InterCode *ICPtr = newICNode(-1);
        ICPtr->kind = IC_LABEL_DEF;
        ICPtr->numOperands = 1;
        ICPtr->operands[0] = L1;

        struct Type *typePtr1 = NULL;
        struct Type *typePtr2 = NULL;
        if (r->children[1]->type == Node_AND)
        {
            typePtr1 = translateCond(r->children[0], L1, labelFalse);
            appendInterCodeToList(ICPtr);
            typePtr2 = translateCond(r->children[2], labelTrue, labelFalse);
        }
        else
        {
            typePtr1 = translateCond(r->children[0], labelTrue, L1);
            appendInterCodeToList(ICPtr);
            typePtr2 = translateCond(r->children[2], labelTrue, labelFalse);
        }
        assert(checkTypeSame(typePtr1, typePtr2) == 1);
        return typePtr1;
    }
    else if ((r->numChildren == 2) && (r->children[0]->type == Node_NOT))
    {
        return translateCond(r->children[1], labelFalse, labelTrue);
    }
    else
    {
        //普通表达式
        struct Operand *tmp = newOperand();
        struct Type *retTypePtr = handleExp(r, tmp, 1); //code1
        //现在exp的值存在了tmp中
        struct InterCode *ICPtr = newICNode(-1);
        ICPtr->numOperands = 3;
        ICPtr->kind = IC_RELOP_GOTO;
        ICPtr->operands[0] = tmp;
        ICPtr->relop = NEQ;
        ICPtr->operands[1] = newOperand();
        ICPtr->operands[1]->kind = OPEARND_CONSTANT;
        ICPtr->operands[1]->info.constantVal = 0;
        ICPtr->operands[2] = labelTrue;
        appendInterCodeToList(ICPtr); //code2

        ICPtr = newICNode(-1);
        ICPtr->numOperands = 1;
        ICPtr->kind = IC_GOTO;
        ICPtr->operands[0] = labelFalse;
        appendInterCodeToList(ICPtr);

        return retTypePtr;
    }
}
struct FieldList *handleArgs(struct TreeNode *r, struct ArgNode **argList)
{
    assert(r->type == Node_Args);
    if (r->numChildren == 1)
    {
        struct Operand *op = newOperand();
        // op->kind = OPEARND_TMP_VAR;
        // op->info.tmpVarNo = getNextTmpNo();
        struct ArgNode *curNode = (struct ArgNode *)malloc(sizeof(struct ArgNode));
        curNode->op = op;
        //Args -> Exp
        struct FieldList *FL = (struct FieldList *)malloc(sizeof(struct FieldList));
        FL->name = NULL;
        FL->type = handleExp(r->children[0], op, 1); //实参的值对应了一个临时变量，一维数组元素需要计算
        FL->line = r->children[0]->line;
        FL->next = NULL;
        //curNode连接到最后成环
        if ((*argList) == NULL)
        {
            (*argList) = curNode;
            curNode->next = curNode->prev = curNode;
        }
        else
        {
            (*argList)->prev->next = curNode;
            curNode->prev = (*argList)->prev;
            curNode->next = (*argList);
            (*argList)->prev = curNode;
        }
        return FL;
    }
    else
    {
        //Args -> Exp Comma Args
        struct Operand *op = newOperand();
        // op->kind = OPEARND_TMP_VAR;
        // op->info.tmpVarNo = getNextTmpNo();
        struct ArgNode *curNode = (struct ArgNode *)malloc(sizeof(struct ArgNode));
        curNode->op = op;

        struct FieldList *FL1 = (struct FieldList *)malloc(sizeof(struct FieldList));
        FL1->type = handleExp(r->children[0], op, 1); //单个
        FL1->name = NULL;
        FL1->line = r->children[1]->line;
        struct FieldList *FL2 = handleArgs(r->children[2], argList);
        FL1->next = FL2;

        if ((*argList) == NULL)
        {
            (*argList) = curNode;
            curNode->next = curNode->prev = curNode;
        }
        else
        {
            (*argList)->prev->next = curNode;
            curNode->prev = (*argList)->prev;
            curNode->next = (*argList);
            (*argList)->prev = curNode;
        }
        return FL1; //连上
    }
}

void appendInterCodeToList(struct InterCode *ICNodePtr)
{
    assert(ICNodePtr != NULL);
    //ICNodePtr只是一个节点
    //for debug
    // ICNodePtr->next = ICNodePtr->prev = ICNodePtr;
    // printICPtr(stderr, ICNodePtr); //for debug
    if (InterCodeList == NULL)
    {
        //初始情况
        InterCodeList = ICNodePtr;
        InterCodeList->next = InterCodeList->prev = InterCodeList;
    }
    else
    {
        ICNodePtr->next = InterCodeList;
        ICNodePtr->prev = InterCodeList->prev;
        InterCodeList->prev->next = ICNodePtr;
        InterCodeList->prev = ICNodePtr;
    }
}

struct InterCode *newICNode(int numOperands)
{
    struct InterCode *ptr = (struct InterCode *)malloc(sizeof(struct InterCode));
    if (numOperands == -1)
    {
        return ptr; //调用者自己完成
    }
    ptr->numOperands = numOperands;
    for (int i = 0; i < numOperands; ++i)
    {
        ptr->operands[i] = (struct Operand *)malloc(sizeof(struct Operand));
    }
    return ptr;
}

//获取新的跳转标号
struct Operand *getNewLabel()
{
    struct Operand *labelOp = (struct Operand *)malloc(sizeof(struct Operand));
    labelOp->kind = OPERAND_LABEL;
    labelOp->info.laeblNo = nrLabel;
    nrLabel++;
    return labelOp;
}

//获取新的临时变量
// struct Operand* getNewTmpVar(){
//     struct Operand* tmpValOp = (struct Operand*)malloc(sizeof(struct Operand));
//     tmpValOp->kind = OPEARND_TMP_VAR;//这个不一定
//     tmpValOp->info.tmpVarNo = nrTmpVar;
//     nrTmpVar++;
//     return tmpValOp;
// }

void printICPtr(FILE *fd, struct InterCode *curPtr)
{
    switch (curPtr->kind)
    {
    case IC_FUNC_DEF:
    {
        assert(curPtr->numOperands == 1);
        fprintf(fd, "FUNCTION %s :\n", curPtr->operands[0]->info.funcName);
    }
    break;
    case IC_PARAM:
    {
        assert(curPtr->numOperands == 1);
        fprintf(fd, "PARAM %s\n", curPtr->operands[0]->info.varName);
    }
    break;
    case IC_DEC:
    {
        assert(curPtr->numOperands == 1);
        fprintf(fd, "DEC %s %d\n", curPtr->operands[0]->info.varName, curPtr->allocSize);
    }
    break;
    case IC_ASSIGN:
    {
        assert(curPtr->numOperands == 2);
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := ");
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, "\n");
    }
    break;
    case IC_GET_ADDR:
    {
        assert((curPtr->numOperands == 2) && (curPtr->operands[1]->kind == OPEARND_VAR));
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := &");
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, "\n");
    }
    break;
    case IC_PLUS:
    {
        assert((curPtr->numOperands == 3));
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := ");
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, " + ");
        printOperand(fd, curPtr->operands[2]);
        fprintf(fd, "\n");
    }
    break;
    case IC_SUB:
    {
        assert((curPtr->numOperands == 3));
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := ");
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, " - ");
        printOperand(fd, curPtr->operands[2]);
        fprintf(fd, "\n");
    }
    break;
    case IC_MUL:
    {
        assert((curPtr->numOperands == 3));
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := ");
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, " * ");
        printOperand(fd, curPtr->operands[2]);
        fprintf(fd, "\n");
    }
    break;
    case IC_DIV:
    {
        assert((curPtr->numOperands == 3));
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := ");
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, " / ");
        printOperand(fd, curPtr->operands[2]);
        fprintf(fd, "\n");
    }
    break;
    case IC_GET_VALUE:
    {
        assert((curPtr->numOperands == 2));
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := *");
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, "\n");
    }
    break;
    case IC_WRITE_TO_ADDR:
    {
        assert((curPtr->numOperands == 2) && (curPtr->operands[0]->kind == OPEARND_ADDR));
        fprintf(fd, "*");
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := ");
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, "\n");
    }
    break;
    case IC_RETURN:
    {
        assert((curPtr->numOperands == 1));
        fprintf(fd, "RETURN ");
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, "\n");
    }
    break;
    case IC_READ:
    {
        assert((curPtr->numOperands == 1) && (curPtr->operands[0]->kind == OPEARND_TMP_VAR));
        fprintf(fd, "READ ");
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, "\n");
    }
    break;
    case IC_WRITE:
    {
        assert((curPtr->numOperands == 1));
        fprintf(fd, "WRITE ");
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, "\n");
    }
    break;
    case IC_ARG:
    {
        assert((curPtr->numOperands == 1));
        //实参有两种类型: 1.普通表达式，值已经存在op对应的临时变量中
        //2. 一维数组Addr。分为从形参得到的数组和局部变量
        struct Operand *op = curPtr->operands[0];
        fprintf(fd, "ARG ");
        //临时变量或普通变量或者常数
        printOperand(fd, op);
        fprintf(fd, "\n");
    }
    break;
    case IC_CALL:
    {
        assert((curPtr->numOperands == 2));
        //返回值存到一个临时变量
        assert(curPtr->operands[0]->kind == OPEARND_TMP_VAR);
        assert(curPtr->operands[1]->kind == OPERAND_FUNC);
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " := CALL %s\n", curPtr->operands[1]->info.funcName);
    }
    break;
    case IC_RELOP_GOTO:
    {
        assert(curPtr->numOperands == 3);
        fprintf(fd, "IF ");
        printOperand(fd, curPtr->operands[0]);
        switch (curPtr->relop)
        {
        case LT:
        {
            fprintf(fd, " < ");
        }
        break;
        case LEQ:
        {
            fprintf(fd, " <= ");
        }
        break;
        case GT:
        {
            fprintf(fd, " > ");
        }
        break;
        case GEQ:
        {
            fprintf(fd, " >= ");
        }
        break;
        case NEQ:
        {
            fprintf(fd, " != ");
        }
        break;
        case EQ:
        {
            fprintf(fd, " == ");
        }
        break;
        default:
        {
            assert(0);
        }
        break;
        }
        printOperand(fd, curPtr->operands[1]);
        fprintf(fd, " GOTO ");
        assert(curPtr->operands[2]->kind == OPERAND_LABEL);
        printOperand(fd, curPtr->operands[2]);
        fprintf(fd, "\n");
    }
    break;
    case IC_LABEL_DEF:
    {
        assert(curPtr->numOperands == 1);
        assert(curPtr->operands[0]->kind == OPERAND_LABEL);
        fprintf(fd, "LABEL ");
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " :\n");
    }
    break;
    case IC_GOTO:
    {
        assert(curPtr->numOperands == 1);
        fprintf(fd, "GOTO ");
        printOperand(fd, curPtr->operands[0]);
        fprintf(fd, " \n");
    }
    break;
    default:
        fprintf(stderr, "printICPtr() need fix!\n");
        break;
    }
}

void printInterCodeList(FILE *fd)
{
    if (InterCodeList == NULL)
    {
        return;
    }
    struct InterCode *curPtr = InterCodeList;
    do
    {
        printICPtr(fd, curPtr);
        curPtr = curPtr->next;
    } while (curPtr != InterCodeList);
}

struct Operand *newOperand()
{
    struct Operand *ptr = (struct Operand *)malloc(sizeof(struct Operand));
    return ptr;
}

void printOperand(FILE *fd, struct Operand *op)
{
    switch (op->kind)
    {
    case OPEARND_VAR:
    {
        fprintf(fd, "%s", op->info.varName);
    }
    break;
    case OPEARND_TMP_VAR:
    case OPEARND_ADDR:
    {
        fprintf(fd, "t%d", op->info.tmpVarNo);
    }
    break;
    case OPEARND_CONSTANT:
    {
        fprintf(fd, "#%d", op->info.constantVal);
    }
    break;
    case OPERAND_LABEL:
    {
        fprintf(fd, "L%d", op->info.laeblNo);
    }
    break;
    default:
        assert(0);
    }
}

void initReadAndWrite()
{
    intTypePtr = (struct Type *)malloc(sizeof(struct Type));
    intTypePtr->kind = BASIC;
    intTypePtr->info.basicType = TYPE_INT;
    struct Func *readFunc = (struct Func *)malloc(sizeof(struct Func));
    readFunc->name = "read";
    readFunc->params = NULL;
    readFunc->retType = intTypePtr;

    struct Func *writeFunc = (struct Func *)malloc(sizeof(struct Func));
    writeFunc->name = "write";
    struct FieldList *FL = (struct FieldList *)malloc(sizeof(struct FieldList));
    FL->next = NULL;
    FL->type = intTypePtr;
    writeFunc->params = FL;
    writeFunc->retType = NULL;

    insertFuncTable(readFunc);
    insertFuncTable(writeFunc);
}

int getNextTmpNo()
{
    //先返回后加
    //临时变量也当成函数局部变量，也要记录相对ebp的位置
    struct Pair *pairPtr = (struct Pair *)malloc(sizeof(struct Pair));
    pairPtr->tmpNo = nrTmpVar;
    pairPtr->offset = curSpace;
    pairPtr->next = NULL;
    insertPairTable(pairPtr);
    curSpace += INT_FLOAT_SIZE;
    return nrTmpVar++;
}

int min(int a, int b)
{
    return a > b ? b : a;
}

void translateToMachineCode(FILE *fd)
{
    if (InterCodeList == NULL)
    {
        return;
    }
    //先打印数据段、read和write这些
    printDataReadWrite(fd);
    struct InterCode *curPtr = InterCodeList;
    do
    {
        //翻译这一句
        translateInterCodeToMachine(fd, curPtr);
        curPtr = curPtr->next;
    } while (curPtr != InterCodeList);
}

void printDataReadWrite(FILE *fd)
{
    fprintf(fd, ".data\n_prompt: .asciiz \"Enter an integer:\"\n_ret: .asciiz \"\\n\"\n.globl main\n.text\n\n");
    fprintf(fd, "read:\n\tli $v0, 4\n\tla $a0, _prompt\n\tsyscall\n\tli $v0, 5\n\tsyscall\n\tjr $ra\n\n");
    fprintf(fd, "write:\n\tli $v0, 1\n\tsyscall\n\tli $v0, 4\n\tla $a0, _ret\n\tsyscall\n\tmove $v0, $0\n\tjr $ra\n");
}

void subSp(FILE *fd, int sz)
{
    fprintf(fd, "\tsubu $sp, $sp, %d\n", sz);
}

void addSp(FILE *fd, int sz)
{
    fprintf(fd, "\taddu $sp, $sp, %d\n", sz);
}

void pushFp(FILE *fd)
{
    //先腾出空间再写
    subSp(fd, 4);
    fprintf(fd, "\tsw $fp, 0($sp)\n");
}

void popFp(FILE *fd)
{
    //先腾出空间再写
    fprintf(fd, "\tlw $fp, 0($sp)\n");
    addSp(fd, 4);
}

int getOffsetToFp(struct Operand *op)
{
    //查找局部变量/参数 或者 临时变量相对fp的偏移
    if (op->kind == OPEARND_VAR)
    {
        struct Variable *varPtr = searchVariableTable(op->info.varName);
        assert(varPtr != NULL);
        return varPtr->offsetToEbp + 4; //因为0(fp)是plf fp
    }
    else
    {
        //临时变量
        assert(op->kind == OPEARND_ADDR || op->kind == OPEARND_TMP_VAR);
        struct Pair *pairPtr = searchPairTable(op->info.tmpVarNo);
        assert(pairPtr != NULL);
        return pairPtr->offset + 4;
    }
}

void loadOffsetFpToTi(FILE *fd, int offset, int i)
{
    //offset 有正负!
    fprintf(fd, "\tlw $t%d, %d($fp)\n", i, offset);
}

void storeTiToOffsetFp(FILE *fd, int offset, int i)
{
    //offset 有正负!
    fprintf(fd, "\tsw $t%d, %d($fp)\n", i, offset);
}

void storeV0ToOffsetFp(FILE *fd, int offset)
{
    //offset 有正负!
    fprintf(fd, "\tsw $v0, %d($fp)\n", offset);
}

void loadOperandToTi(FILE *fd, struct Operand *op, int i)
{
    //常量、临时变量、参数、地址
    switch (op->kind)
    {
    case OPEARND_CONSTANT:
    {
        fprintf(fd, "\tli $t%d, %d\n", i, op->info.constantVal);
    }
    break;
    case OPEARND_ADDR:
    case OPEARND_VAR:
    case OPEARND_TMP_VAR:
    {
        //临时变量在栈中
        int offset = getOffsetToFp(op);
        loadOffsetFpToTi(fd, -offset, i);
    }
    break;
    default:
        fprintf(stderr, "loadOperandToTi error!\n");
        assert(0);
        break;
    }
}

//将ti寄存器的值push
void pushTi(FILE *fd, int i)
{
    subSp(fd, 4);
    fprintf(fd, "\tsw $t%d, 0($sp)\n", i);
}

//push ra
void pushRa(FILE *fd)
{
    subSp(fd, 4);
    fprintf(fd, "\tsw $ra, 0($sp)\n");
}

void popRa(FILE *fd)
{
    fprintf(fd, "\tlw $ra, 0($sp)\n");
    addSp(fd, 4);
}

void translateInterCodeToMachine(FILE *fd, struct InterCode *curPtr)
{
    static struct Func *curFuncPtr = NULL; //在return中，要回收函数栈空间等操作，需要用到函数信息
    static int preNrArg = 0;
    switch (curPtr->kind)
    {
    case IC_FUNC_DEF:
    {
        assert(curPtr->numOperands == 1);
        curFuncPtr = searchFuncTable(curPtr->operands[0]->info.funcName);
        assert(curFuncPtr != NULL);
        //函数名称
        fprintf(fd, "\n%s:\n", curFuncPtr->name);
        //push fp
        pushFp(fd);
        fprintf(fd, "\tmove $fp, $sp\n"); //设置fp
        //为参数和局部变量、临时变量腾出空间
        subSp(fd, curFuncPtr->varSpace);
        //把参数从寄存器、栈复制到这里
        for (int i = curFuncPtr->nrParams - 1; i >= 0; --i)
        {
            //如果是倒数四个之内，从寄存器找
            int belowFpOffset = (i + 1) * 4;
            if (curFuncPtr->nrParams - i <= 4)
            {
                //从寄存器拿
                int retIdx = curFuncPtr->nrParams - 1 - i;
                fprintf(fd, "\tsw $a%d, -%d($fp)\n", retIdx, belowFpOffset);
            }
            else
            {
                //从栈拿
                int aboveFpOffset = i * 4 + 8;
                fprintf(fd, "\tlw $t0, %d($fp)\n", aboveFpOffset);
                fprintf(fd, "\tsw $t0, -%d($fp)\n", belowFpOffset);
            }
        }
    }
    break;
    case IC_PARAM:
    {
        assert(curPtr->numOperands == 1);
        //do nothing
    }
    break;
    case IC_DEC:
    {
        assert(curPtr->numOperands == 1);
        //do nothing
    }
    break;
    case IC_ASSIGN:
    {
        assert(curPtr->numOperands == 2);
        //会出现右侧是立即数，怎么办呢? 使用li将立即数放入寄存器t1中? 考虑写一个loadToRegI函数，将operand的值让如指定寄存器中
        //x := y.
        //但是对于 1.临时变量 2. 局部变量/参数 的查找是不同的
        loadOperandToTi(fd, curPtr->operands[1], 0); //右侧值存到t0
        int xOffsetToFp = getOffsetToFp(curPtr->operands[0]);
        storeTiToOffsetFp(fd, -xOffsetToFp, 0);
    }
    break;
    case IC_GET_ADDR:
    {
        assert((curPtr->numOperands == 2) && (curPtr->operands[1]->kind == OPEARND_VAR));
        //x := &y
        //y是一个变量，他在栈是有相对于fp的位置的，可以用subu x fp offset 实现
        int yOffset = getOffsetToFp(curPtr->operands[1]);
        int xOffset = getOffsetToFp(curPtr->operands[0]);
        //把fp - offset的值存在t0中
        fprintf(fd, "\tsubu $t0, $fp, %d\n", yOffset);
        //把t0的值写在x的位置
        storeTiToOffsetFp(fd, -xOffset, 0);
    }
    break;
    case IC_PLUS:
    {
        assert((curPtr->numOperands == 3));
        //x := y + z
        //把y,z的放入t0,t1
        loadOperandToTi(fd, curPtr->operands[1], 0);
        loadOperandToTi(fd, curPtr->operands[2], 1);
        //相加放入t2
        fprintf(fd, "\tadd $t2, $t0, $t1\n");
        //t2放入x
        int xOffset = getOffsetToFp(curPtr->operands[0]);
        storeTiToOffsetFp(fd, -xOffset, 2);
    }
    break;
    case IC_SUB:
    {
        assert((curPtr->numOperands == 3));
        assert((curPtr->numOperands == 3));
        //x := y - z
        //把y,z的放入t0,t1
        loadOperandToTi(fd, curPtr->operands[1], 0);
        loadOperandToTi(fd, curPtr->operands[2], 1);
        //相减放入t2
        fprintf(fd, "\tsub $t2, $t0, $t1\n");
        //t2放入x
        int xOffset = getOffsetToFp(curPtr->operands[0]);
        storeTiToOffsetFp(fd, -xOffset, 2);
    }
    break;
    case IC_MUL:
    {
        assert((curPtr->numOperands == 3));
        //x := y * z
        //把y,z的放入t0,t1
        loadOperandToTi(fd, curPtr->operands[1], 0);
        loadOperandToTi(fd, curPtr->operands[2], 1);
        //相减放入t2
        fprintf(fd, "\tmul $t2, $t0, $t1\n");
        //t2放入x
        int xOffset = getOffsetToFp(curPtr->operands[0]);
        storeTiToOffsetFp(fd, -xOffset, 2);
    }
    break;
    case IC_DIV:
    {
        assert((curPtr->numOperands == 3));
        //x := y / z
        //把y,z的放入t0,t1
        loadOperandToTi(fd, curPtr->operands[1], 0);
        loadOperandToTi(fd, curPtr->operands[2], 1);
        fprintf(fd, "\tdiv $t0, $t1\n");
        fprintf(fd, "\tmflo $t2\t\n");
        //t2放入x
        int xOffset = getOffsetToFp(curPtr->operands[0]);
        storeTiToOffsetFp(fd, -xOffset, 2);
    }
    break;
    case IC_GET_VALUE:
    {
        assert((curPtr->numOperands == 2));
        //x := *y
        //把 y的值取出放入t0
        loadOperandToTi(fd, curPtr->operands[1], 0);
        //对t0取值放入t1
        fprintf(fd, "\tlw $t1, 0($t0)\n");
        //t1写入x
        int xOffset = getOffsetToFp(curPtr->operands[0]);
        storeTiToOffsetFp(fd, -xOffset, 1);
    }
    break;
    case IC_WRITE_TO_ADDR:
    {
        assert((curPtr->numOperands == 2) && (curPtr->operands[0]->kind == OPEARND_ADDR));
        //把x,y的放入t0,t1
        loadOperandToTi(fd, curPtr->operands[0], 0);
        loadOperandToTi(fd, curPtr->operands[1], 1);
        //*x  := y
        fprintf(fd, "\tsw $t1, 0($t0)\n");
    }
    break;
    case IC_RETURN:
    {
        assert((curPtr->numOperands == 1));
        //TODO 设置v0
        //return x
        //把返回值x得到存入t0
        loadOperandToTi(fd, curPtr->operands[0], 0);
        fprintf(fd, "\tmove $v0, $t0\n");
        //sp加上space
        addSp(fd, curFuncPtr->varSpace);
        //pop fp，恢复old fp
        popFp(fd);
        //跳转到$ra
        fprintf(fd, "\tjr $ra\n");
    }
    break;
    case IC_READ:
    {
        assert((curPtr->numOperands == 1) && (curPtr->operands[0]->kind == OPEARND_TMP_VAR));
        //read x
        //call read
        pushRa(fd);
        fprintf(fd, "\tjal read\n");
        int xOffset = getOffsetToFp(curPtr->operands[0]);
        storeV0ToOffsetFp(fd, -xOffset);
        //pop $ra
        popRa(fd);
    }
    break;
    case IC_WRITE:
    {
        assert((curPtr->numOperands == 1));
        //write x
        //要取得x放入t0
        loadOperandToTi(fd, curPtr->operands[0], 0);
        fprintf(fd, "\tmove $a0, $t0\n");
        pushRa(fd);
        fprintf(fd, "\tjal write\n");
        popRa(fd);
    }
    break;
    case IC_ARG:
    {
        assert((curPtr->numOperands == 1));
        //ARG x
        //看这是第几个连续的ARG, >= 5就要push
        if (preNrArg >= 4)
        {
            //存到t0
            loadOperandToTi(fd, curPtr->operands[0], 0);
            //push t0
            pushTi(fd, 0);
        }
        else
        {
            //存到T_preNrArg寄存器中
            //存到t0
            loadOperandToTi(fd, curPtr->operands[0], 0);
            fprintf(fd, "\tmove $a%d, $t0\n", preNrArg);
        }
    }
    break;
    case IC_CALL:
    {
        assert((curPtr->numOperands == 2));
        //ARG 已经完成了
        //x := call f
        //push %ra
        pushRa(fd);
        fprintf(fd, "\tjal %s\n", curPtr->operands[1]->info.funcName);
        //结果在$v0中
        int xOffset = getOffsetToFp(curPtr->operands[0]);
        storeV0ToOffsetFp(fd, -xOffset);
        //pop $ra
        popRa(fd);
        //回收ra,arg的空间sp。注意sp的+sz是每个ARG一次次-4完成的
        struct Func *funcPtr = searchFuncTable(curPtr->operands[1]->info.funcName);
        assert(funcPtr != NULL);
        //仅当nrParam >= 5才需要,否则都是在寄存器中
        int sz = (funcPtr->nrParams - 4) * INT_FLOAT_SIZE;
        if (sz > 0)
        {
            addSp(fd, sz);
        }
    }
    break;
    case IC_RELOP_GOTO:
    {
        assert(curPtr->numOperands == 3);
        //if x relop y goto z
        //把x,y的放入t0,t1
        loadOperandToTi(fd, curPtr->operands[0], 0);
        loadOperandToTi(fd, curPtr->operands[1], 1);
        switch (curPtr->relop)
        {
        case LT:
        {
            fprintf(fd, "\tblt ");
        }
        break;
        case LEQ:
        {
            fprintf(fd, "\tble ");
        }
        break;
        case GT:
        {
            fprintf(fd, "\tbgt ");
        }
        break;
        case GEQ:
        {
            fprintf(fd, "\tbge ");
        }
        break;
        case NEQ:
        {
            fprintf(fd, "\tbne ");
        }
        break;
        case EQ:
        {
            fprintf(fd, "\tbeq ");
        }
        break;
        default:
        {
            assert(0);
        }
        break;
        }
        fprintf(fd, "$t0, $t1, L%d\n", curPtr->operands[2]->info.laeblNo);
    }
    break;
    case IC_LABEL_DEF:
    {
        assert(curPtr->numOperands == 1);
        fprintf(fd, "L%d:\n", curPtr->operands[0]->info.laeblNo);
    }
    break;
    case IC_GOTO:
    {
        assert(curPtr->numOperands == 1);
        fprintf(fd, "\tj L%d\n", curPtr->operands[0]->info.laeblNo);
    }
    break;
    default:
        break;
    }
    if (curPtr->kind == IC_ARG)
    {
        preNrArg++;
    }
    else
    {
        preNrArg = 0;
    }
}