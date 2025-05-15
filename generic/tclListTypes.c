/*
 * tclListTypes.c --
 *
 *	This file contains functions that implement the Tcl abstract list
 *	object types.
 *
 * Copyright (c) 2025 Ashok P. Nadkarni.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <assert.h>
#include "tclInt.h"

/*
 * Since iterating is a little slower for abstract lists, we use a
 * threshold to decide when to use the abstract list type. This is
 * a tradeoff between memory usage and speed.
 */
#define LREVERSE_LENGTH_THRESHOLD 100
#define LREPEAT_LENGTH_THRESHOLD 100
#define LRANGE_LENGTH_THRESHOLD 100

/*
 * TclObjArray stores a reference counted Tcl_Obj array.
 */
typedef struct TclObjArray {
    Tcl_Size refCount;   /* Reference count */
    Tcl_Size nelems;     /* Number of elements in the array */
    Tcl_Obj *elemPtrs[1]; /* Variable size array */
} TclObjArray;

/*
 * Allocate a new TclObjArray structure and initialize it with the
 * given Tcl_Obj elements, incrementing their reference counts.
 * The reference count of the array itself is initialized to 0.
 */
static TclObjArray *
TclObjArrayNew(size_t nelems, Tcl_Obj * const elemPtrs[])
{
    TclObjArray *arrayPtr = (TclObjArray *)Tcl_Alloc(
	sizeof(TclObjArray) + (nelems - 1) * sizeof(Tcl_Obj *));
    for (size_t i = 0; i < nelems; i++) {
        Tcl_IncrRefCount(elemPtrs[i]);
        arrayPtr->elemPtrs[i] = elemPtrs[i];
    }
    arrayPtr->refCount = 0;
    arrayPtr->nelems = nelems;
    return arrayPtr;
}

/* Add a reference to a TclObjArray */
static inline void
TclObjArrayRef(TclObjArray *arrayPtr)
{
    arrayPtr->refCount++;
}

/* Frees a TclObjArray structure irrespective of the reference count. */
static void
TclObjArrayFree(TclObjArray *arrayPtr)
{
    for (Tcl_Size i = 0; i < arrayPtr->nelems; i++) {
	Tcl_DecrRefCount(arrayPtr->elemPtrs[i]);
    }
    Tcl_Free(arrayPtr);
}

/*
 * Remove a reference from an TclObjArray, freeing it if no more remain.
 * The reference count of the elements is decremented as well in that case.
 */
static inline void
TclObjArrayUnref(TclObjArray *arrayPtr)
{
    if (arrayPtr->refCount <= 1) {
	TclObjArrayFree(arrayPtr);
    } else {
	arrayPtr->refCount--;
    }
}


/* Returns count of elements in array and pointer to them in objPtrPtr */
static inline Tcl_Size
TclObjArrayElems(TclObjArray *arrayPtr, Tcl_Obj ***objPtrPtr)
{
    *objPtrPtr = arrayPtr->elemPtrs;
    return arrayPtr->nelems;
}

/* FUTURES - move to tclInt.h and use in other list implementations as well */
static inline Tcl_Size
TclNormalizeRangeLimits(Tcl_Size *startPtr, Tcl_Size *endPtr, Tcl_Size len)
{
    assert(len >= 0);
    if (*startPtr < 0) {
	*startPtr = 0;
    }
    if (*endPtr >= len) {
	*endPtr = len - 1;
    }
    if (*startPtr > *endPtr) {
	*endPtr = *startPtr - 1;
    }
    return *endPtr - *startPtr + 1;
}

/*
 *------------------------------------------------------------------------
 *
 * TclAbstractListUpdateString --
 *
 *    Common function to update the string representation of an abstract list
 *    type. Adapted from UpdateStringOfList in tclListObj.c.
 *    Assumes no prior string representation exists.
*
 * Results:
 *    None.
 *
 * Side effects:
 *    The string representation of the object is updated.
 *
 *------------------------------------------------------------------------
 */
static void TclAbstractListUpdateString (Tcl_Obj *objPtr)
{
   #define LOCAL_SIZE 64
    char localFlags[LOCAL_SIZE], *flagPtr = NULL;
    Tcl_Size numElems, i, length;
    size_t bytesNeeded = 0;
    const char *elem;
    char *start, *dst;
    int ret;

    ret = Tcl_ListObjLength(NULL, objPtr, &numElems);
    assert(ret == TCL_OK); // Should only be called for lists
    (void) ret; // Avoid compiler warning

    /* Handle empty list case first, so rest of the routine is simpler. */

    if (numElems == 0) {
	objPtr->bytes = (char *)Tcl_Alloc(1);
	objPtr->bytes[0] = '\0';
	objPtr->length = 0;
	return;
    }

    /* Pass 1: estimate space, gather flags. */
    if (numElems <= LOCAL_SIZE) {
        flagPtr = localFlags;
    }
    else {
        flagPtr = (char *)Tcl_Alloc(numElems);
    }
    for (i = 0; i < numElems; i++) {
        Tcl_Obj *elemObj;
        flagPtr[i] = (i ? TCL_DONT_QUOTE_HASH : 0);
        ret = Tcl_ListObjIndex(NULL, objPtr, i, &elemObj);
        assert(ret == TCL_OK);
        elem       = Tcl_GetStringFromObj(elemObj, &length);
        bytesNeeded += TclScanElement(elem, length, flagPtr + i);
        if (bytesNeeded > SIZE_MAX - numElems) {
            Tcl_Panic("max size for a Tcl value (%" TCL_Z_MODIFIER
                      "u bytes) exceeded",
                      SIZE_MAX);
        }
#if TCL_MAJOR_VERSION > 8
        Tcl_BounceRefCount(elemObj);
#endif
    }
    bytesNeeded += numElems; /* Including trailing nul */

    /*
     * Pass 2: copy into string rep buffer.
     */

    start = dst = (char *) Tcl_Alloc(bytesNeeded);
    for (i = 0; i < numElems; i++) {
        Tcl_Obj *elemObj;
        flagPtr[i] |= (i ? TCL_DONT_QUOTE_HASH : 0);
        ret = Tcl_ListObjIndex(NULL, objPtr, i, &elemObj);
        assert(ret == TCL_OK);
        elem = Tcl_GetStringFromObj(elemObj, &length);
        dst += TclConvertElement(elem, length, dst, flagPtr[i]);
        *dst++ = ' ';
    }
    dst[-1]         = '\0'; /* Overwrite last space */
    size_t finalLen = dst - start; /* Includes trailing nul */

    /* If we are wasting "too many" bytes, attempt a reallocation */
    if (bytesNeeded > 1000 && (bytesNeeded-finalLen) > (bytesNeeded/4)) {
        char *newBytes = (char *)Tcl_Realloc(start, finalLen);
        if (newBytes != NULL) {
            start = newBytes;
        }
    }
    objPtr->bytes = start;
    objPtr->length = finalLen-1; /* Exclude the trailing null */

    if (flagPtr != localFlags) {
        Tcl_Free(flagPtr);
    }
}

/*
 * lreverseType -
 *
 * ------------------------------------------------------------------------
 * lreverseType is an abstract list type that contains the same elements as a
 * given list but in reverse order. Implementation is straightforward with the
 * target list stored in ptrAndSize.ptr field. Indexing is then just a question
 * of mapping index of the reversed list to that of the original target.
 * The ptrAndSize.size field is used as a length cache.
 * ------------------------------------------------------------------------
 */

static void LreverseFreeIntrep(Tcl_Obj *objPtr);
static void LreverseDupIntrep(Tcl_Obj *srcObj, Tcl_Obj *dupObj);
static Tcl_ObjTypeLengthProc LreverseTypeLength;
static Tcl_ObjTypeIndexProc LreverseTypeIndex;
static Tcl_ObjTypeReverseProc LreverseTypeReverse;

/*
 * IMPORTANT - current implementation is read-only except for reverseProc.
 * That is, the functions below that set or modify elements must be NULL. If
 * you change this, be aware that both the object and internal
 * representation (targetObj) may be shared and must be checked before
 * modification.
 */
static const Tcl_ObjType lreverseType = {
    "reversedList",                     /* name */
    LreverseFreeIntrep,                 /* freeIntRepProc */
    LreverseDupIntrep,                  /* dupIntRepProc */
    TclAbstractListUpdateString,        /* updateStringProc */
    NULL,                               /* setFromAnyProc */
    TCL_OBJTYPE_V2(LreverseTypeLength,  /* lengthProc */
		   LreverseTypeIndex,   /* indexProc */
		   NULL,                /* sliceProc */
		   LreverseTypeReverse, /* reverseProc */
		   NULL,                /* getElementsProc */
		   NULL,                /* setElementProc - FUTURES */
		   NULL,                /* replaceProc - FUTURES */
		   NULL)                /* inOperProc - FUTURES */
};

void
LreverseFreeIntrep(Tcl_Obj *objPtr)
{
    Tcl_DecrRefCount((Tcl_Obj *)objPtr->internalRep.ptrAndSize.ptr);
}

void
LreverseDupIntrep(Tcl_Obj *srcObj, Tcl_Obj *dupObj)
{
    Tcl_Obj *targetObj = (Tcl_Obj *)srcObj->internalRep.ptrAndSize.ptr;
    Tcl_IncrRefCount(targetObj);
    dupObj->internalRep.ptrAndSize.ptr = targetObj;
    dupObj->internalRep.ptrAndSize.size = srcObj->internalRep.ptrAndSize.size;
    dupObj->typePtr = srcObj->typePtr;
}

/* Implementation of Tcl_ObjType.lengthProc for lreverseType */
Tcl_Size
LreverseTypeLength(Tcl_Obj *objPtr)
{
    return objPtr->internalRep.ptrAndSize.size;
}

/* Implementation of Tcl_ObjType.indexProc for lreverseType */
int
LreverseTypeIndex(Tcl_Interp *interp,
    Tcl_Obj *objPtr, /* Source list */
    Tcl_Size index,  /* Element index */
    Tcl_Obj **elemPtrPtr) /* Returned element */
{
    Tcl_Obj *targetPtr = (Tcl_Obj *)objPtr->internalRep.ptrAndSize.ptr;
    Tcl_Size len = objPtr->internalRep.ptrAndSize.size;
    if (index < 0 || index >= len) {
        *elemPtrPtr = NULL;
        return TCL_OK;
    }
    index = len - index - 1; /* Reverse the index */
    return Tcl_ListObjIndex(interp, targetPtr, index, elemPtrPtr);
}

/* Implementation of Tcl_ObjType.reverseProc for lreverseType */
int
LreverseTypeReverse(Tcl_Interp *interp,
    Tcl_Obj *objPtr,          /* Operand */
    Tcl_Obj **reversedPtrPtr) /* Result */
{
    (void)interp; /* Unused */
    /* Simple return the original */
    *reversedPtrPtr = (Tcl_Obj *) objPtr->internalRep.ptrAndSize.ptr;
    return TCL_OK;
}

/*
 *------------------------------------------------------------------------
 *
 * Tcl_ListObjReverse --
 *
 *    Returns a Tcl_Obj containing a list with the same elements as the
 *    source list with elements in reverse order.
 *
 * Results:
 *    Standard Tcl result.
 *
 * Side effects:
 *    Stores the result in *reversedPtrPtr. This may be the same as objPtr,
 *    a new allocation, or a pointer to an internally stored object. In
 *    all cases, the reference count of the returned object is not
 *    incremented to account for the returned reference to it.
 *
 *------------------------------------------------------------------------
 */
int
Tcl_ListObjReverse(
    Tcl_Interp *interp,
    Tcl_Obj *objPtr,          /* Source whose elements are to be reversed */
    Tcl_Obj **reversedPtrPtr) /* Location to store result object */
{
    /* If the list is an AbstractList with a specialized reverse, use it. */
    if (TclObjTypeHasProc(objPtr, reverseProc)) {
	if (TclObjTypeReverse(interp, objPtr, reversedPtrPtr) == TCL_OK) {
	    return TCL_OK;
	}
	/* Specialization does not work for this case. Try default path */
    }

    Tcl_Size elemc;

    /* Verify target is a list or can be converted to one */
    if (TclObjTypeHasProc(objPtr, lengthProc)) {
	elemc = TclObjTypeLength(objPtr);
    } else {
	if (TclListObjLength(interp, objPtr, &elemc) != TCL_OK) {
	    return TCL_ERROR;
	}
    }

    /* If the list is empty, just return it. [Bug 1876793] */
    if (elemc == 0) {
	*reversedPtrPtr = objPtr;
	return TCL_OK;
    }

    Tcl_Obj *resultPtr;
    if (elemc >= LREVERSE_LENGTH_THRESHOLD || objPtr->typePtr != &tclListType) {
	TclNewObj(resultPtr);
	TclInvalidateStringRep(resultPtr);

	Tcl_IncrRefCount(objPtr);
	resultPtr->internalRep.ptrAndSize.ptr = objPtr;
	resultPtr->internalRep.ptrAndSize.size = elemc;
	resultPtr->typePtr = &lreverseType;
	*reversedPtrPtr = resultPtr;
	return TCL_OK;
    }

    /* Non-abstract list small enough to copy. */

    Tcl_Obj **elemv;

    if (TclListObjGetElements(interp, objPtr, &elemc, &elemv) != TCL_OK) {
	return TCL_ERROR;
    }
    resultPtr = Tcl_NewListObj(elemc, NULL);
    Tcl_Obj **dataArray = NULL;
    ListRep listRep;
    ListObjGetRep(resultPtr, &listRep);
    dataArray = ListRepElementsBase(&listRep);
    assert(dataArray);
    listRep.storePtr->numUsed = elemc;
    if (listRep.spanPtr) {
	/* Future proofing in case Tcl_NewListObj returns a span */
	listRep.spanPtr->spanStart = listRep.storePtr->firstUsed;
	listRep.spanPtr->spanLength = listRep.storePtr->numUsed;
    }
    for (Tcl_Size i = 0; i < elemc; i++) {
        Tcl_IncrRefCount(elemv[i]);
        dataArray[elemc - i - 1] = elemv[i];
    }

    *reversedPtrPtr = resultPtr;
    return TCL_OK;
}

/*
 * lrepeatType -
 *
 * ------------------------------------------------------------------------
 * lrepeatType is an abstract list type that repeated elements.
 * Implementation is straightforward with the elements stored in
 * list stored in ptrAndSize.ptr and number of repetitions in
 * ptrAndSize.size fields. Indexing is then just a question
 * of mapping index of modulo length of list of repeated elements.
 * ------------------------------------------------------------------------
 */

static void LrepeatFreeIntrep(Tcl_Obj *objPtr);
static void LrepeatDupIntrep(Tcl_Obj *srcObj, Tcl_Obj *dupObj);
static Tcl_ObjTypeLengthProc LrepeatTypeLength;
static Tcl_ObjTypeIndexProc LrepeatTypeIndex;

/*
 * IMPORTANT - current implementation is read-only. That is, the
 * functions below that set or modify elements are NULL. If you change
 * this, be aware that both the object and internal representation
 * may be shared must be checked before modification.
 */
static const Tcl_ObjType lrepeatType = {
    "repeatedList",                   /* name */
    LrepeatFreeIntrep,                /* freeIntRepProc */
    LrepeatDupIntrep,                 /* dupIntRepProc */
    TclAbstractListUpdateString,      /* updateStringProc */
    NULL,                             /* setFromAnyProc */
    TCL_OBJTYPE_V2(LrepeatTypeLength, /* lengthProc */
		   LrepeatTypeIndex,  /* indexProc */
		   NULL,              /* sliceProc */
		   NULL,              /* Must be NULL - see above comment */
		   NULL,              /* getElementsProc */
		   NULL,              /* Must be NULL - see above comment */
		   NULL,              /* Must be NULL - see above comment */
		   NULL)              /* inOperProc - FUTURES */
};

void
LrepeatFreeIntrep(Tcl_Obj *objPtr)
{
    TclObjArrayUnref((TclObjArray *)objPtr->internalRep.ptrAndSize.ptr);
}

void
LrepeatDupIntrep(Tcl_Obj *srcObj, Tcl_Obj *dupObj)
{
    TclObjArray *arrayPtr = (TclObjArray *)srcObj->internalRep.ptrAndSize.ptr;
    TclObjArrayRef(arrayPtr);
    dupObj->internalRep.ptrAndSize.ptr = arrayPtr;
    dupObj->internalRep.ptrAndSize.size = srcObj->internalRep.ptrAndSize.size;
    dupObj->typePtr = srcObj->typePtr;
}

/* Implementation of Tcl_ObjType.lengthProc for lrepeatType */
Tcl_Size
LrepeatTypeLength(Tcl_Obj *objPtr)
{
    return objPtr->internalRep.ptrAndSize.size;
}

/* Implementation of Tcl_ObjType.indexProc for lrepeatType */
int
LrepeatTypeIndex(
    Tcl_Interp *interp,
    Tcl_Obj *objPtr, /* Source list */
    Tcl_Size index,  /* Element index */
    Tcl_Obj **elemPtrPtr) /* Returned element */
{
    (void) interp; /* Unused */
    Tcl_Size len = objPtr->internalRep.ptrAndSize.size;
    if (index < 0 || index >= len) {
        *elemPtrPtr = NULL;
        return TCL_OK;
    }
    TclObjArray *arrayPtr = (TclObjArray *)objPtr->internalRep.ptrAndSize.ptr;
    Tcl_Obj **elems;
    Tcl_Size arraySize = TclObjArrayElems(arrayPtr, &elems);
    index = index % arraySize; /* Modulo the size of the array */
    *elemPtrPtr = arrayPtr->elemPtrs[index];
    return TCL_OK;
}

/*
 *------------------------------------------------------------------------
 *
 * Tcl_ListObjRepeat --
 *
 *    Returns a Tcl_Obj containing a list whose elements are the same as the
 *    passed items repeated a given number of times.
 *
 * Results:
 *    Standard Tcl result.
 *
 * Side effects:
 *    Stores the result in *reversedPtrPtr. This may be the same as objPtr,
 *    a new allocation, or a pointer to an internally stored object. In
 *    all cases, the reference count of the returned object is not
 *    incremented to account for the returned reference to it.
 *
 *------------------------------------------------------------------------
 */
int
Tcl_ListObjRepeat(
    Tcl_Interp *interp,
    Tcl_Size repeatCount,   /* Number of times to repeat */
    Tcl_Size objc,          /* Number of elements in objv */
    Tcl_Obj *const objv[],  /* Source whose elements are to be repeated */
    Tcl_Obj **resultPtrPtr) /* Location to store result object */
{
    if (repeatCount < 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad count \"%" TCL_SIZE_MODIFIER "d\": must be integer >= 0", repeatCount));
	Tcl_SetErrorCode(interp, "TCL", "OPERATION", "LREPEAT", "NEGARG",
		(char *)NULL);
	return TCL_ERROR;
    }

    Tcl_Size totalElems = objc * repeatCount;
    if (totalElems == 0) {
	TclNewObj(*resultPtrPtr);
	return TCL_OK;
    }

    /* Final sanity check. Do not exceed limits on max list length. */
    if (objc > LIST_MAX/repeatCount) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"max length of a Tcl list (%" TCL_SIZE_MODIFIER "d elements) exceeded", LIST_MAX));
	Tcl_SetErrorCode(interp, "TCL", "MEMORY", (char *)NULL);
	return TCL_ERROR;
    }

    Tcl_Obj *resultPtr;
    if (totalElems >= LREPEAT_LENGTH_THRESHOLD) {
	TclObjArray *arrayPtr = TclObjArrayNew(objc, objv);
	TclNewObj(resultPtr);
	arrayPtr->refCount++;
	TclInvalidateStringRep(resultPtr);
	resultPtr->internalRep.ptrAndSize.ptr = arrayPtr;
	resultPtr->internalRep.ptrAndSize.size = totalElems;
	resultPtr->typePtr = &lrepeatType;
	*resultPtrPtr = resultPtr;
	return TCL_OK;
    }

    /* For small lists, create a copy as indexing is slightly faster */
    resultPtr = Tcl_NewListObj(totalElems, NULL);
    Tcl_Obj **dataArray = NULL;
    if (totalElems) {
	ListRep listRep;
	ListObjGetRep(resultPtr, &listRep);
	dataArray = ListRepElementsBase(&listRep);
	listRep.storePtr->numUsed = totalElems;
	if (listRep.spanPtr) {
	    /* Future proofing in case Tcl_NewListObj returns a span */
	    listRep.spanPtr->spanStart = listRep.storePtr->firstUsed;
	    listRep.spanPtr->spanLength = listRep.storePtr->numUsed;
	}
    }

    /*
     * Set the elements. Note that we handle the common degenerate case of a
     * single value being repeated separately to permit the compiler as much
     * room as possible to optimize a loop that might be run a very large
     * number of times.
     */

    assert(dataArray || totalElems == 0 );
    if (objc == 1) {
	Tcl_Obj *tmpPtr = objv[0];

	tmpPtr->refCount += repeatCount;
	for (Tcl_Size i=0 ; i<totalElems ; i++) {
	    dataArray[i] = tmpPtr;
	}
    } else {
	for (Tcl_Size i = 0, k = 0; i < repeatCount; i++) {
	    for (Tcl_Size j=0 ; j<objc ; j++) {
		Tcl_IncrRefCount(objv[j]);
		dataArray[k++] = objv[j];
	    }
	}
    }
    *resultPtrPtr = resultPtr;
    return TCL_OK;
}

/*
 * ------------------------------------------------------------------------
 * lrangeType -
 *
 * lrangeType is an abstract list type holding a range of elements from a
 * given list. The range is specified by a start index and count of elements.
 * The type is a descriptor stored in the twoPtrValue.ptr1 field of Tcl_Obj.
 * ------------------------------------------------------------------------
 */
typedef struct LrangeRep {
    Tcl_Obj *srcListPtr; /* Source list */
    Tcl_Size refCount;   /* Reference count */
    Tcl_Size srcIndex;   /* Start index of range in source list */
    Tcl_Size rangeLen;   /* Number of elements in range */
} LrangeRep;

static void LrangeFreeIntrep(Tcl_Obj *objPtr);
static void LrangeDupIntrep(Tcl_Obj *srcObj, Tcl_Obj *dupObj);
static Tcl_ObjTypeLengthProc LrangeTypeLength;
static Tcl_ObjTypeIndexProc LrangeTypeIndex;
static Tcl_ObjTypeSliceProc LrangeSlice;

/*
 * IMPORTANT - current implementation is read-only. That is, the
 * functions below that set or modify elements are NULL. If you change
 * this, be aware that both the object and internal representation
 * may be shared and must be checked before modification.
 */
static const Tcl_ObjType lrangeType = {
    "rangeList",                     /* name */
    LrangeFreeIntrep,                /* freeIntRepProc */
    LrangeDupIntrep,                 /* dupIntRepProc */
    TclAbstractListUpdateString,     /* updateStringProc */
    NULL,                            /* setFromAnyProc */
    TCL_OBJTYPE_V2(LrangeTypeLength, /* lengthProc */
		   LrangeTypeIndex,  /* indexProc */
		   LrangeSlice,      /* sliceProc */
		   NULL,             /* reverseProc, see above comment */
		   NULL,             /* getElementsProc */
		   NULL,             /* setElementProc, see above comment */
		   NULL,             /* replaceProc, see above comment */
		   NULL)             /* inOperProc - FUTURES */
};

static inline int
LrangeMeetsLengthCriteria(
    Tcl_Size rangeLen,
    Tcl_Size srcLen)
{
    /*
     * To use lrangeType, the range length
     * - must not be much smaller (1/2?) than the source list as else
     *   it will potentially hold on to the Tcl_Obj's in the source list
     *   that are not within the range longer than necessary after the
     *   original source list is freed.
     * - is at least LRANGE_LENGTH_THRESHOLD elements long as otherwise the
     *   memory savings is (probably) not worth the extra overhead of the
     *   accessing the abstract list.
     */
    return (rangeLen >= LRANGE_LENGTH_THRESHOLD &&
            rangeLen >= srcLen / 2);
}

/* Returns a new lrangeType object that references the source list */
static int
LrangeNew(
    Tcl_Obj *srcPtr,    /* Source for the range */
    Tcl_Size srcIndex,  /* Start of range in srcPtr */
    Tcl_Size rangeLen,  /* Length of range */
    Tcl_Obj **resultPtrPtr) /* Location to store range object */
{
    assert(srcIndex >= 0);
    assert(rangeLen >= 0);

    /* Create a lrangeType referencing the original source list */
    LrangeRep *repPtr = (LrangeRep *)Tcl_Alloc(sizeof(LrangeRep));
    Tcl_Obj *resultPtr;
    Tcl_IncrRefCount(srcPtr);
    repPtr->refCount = 1;
    repPtr->srcListPtr = srcPtr;
    repPtr->srcIndex = srcIndex;
    repPtr->rangeLen = rangeLen;
    TclNewObj(resultPtr);
    TclInvalidateStringRep(resultPtr);
    resultPtr->internalRep.twoPtrValue.ptr1 = repPtr;
    resultPtr->internalRep.twoPtrValue.ptr2 = NULL;
    resultPtr->typePtr = &lrangeType;
    *resultPtrPtr = resultPtr;
    return TCL_OK;

}

void
LrangeFreeIntrep(Tcl_Obj *objPtr)
{
    LrangeRep *repPtr = (LrangeRep *)objPtr->internalRep.twoPtrValue.ptr1;
    if (repPtr->refCount <= 1) {
	Tcl_DecrRefCount(repPtr->srcListPtr);
	Tcl_Free(repPtr);
    } else {
	repPtr->refCount--;
    }
}

void
LrangeDupIntrep(Tcl_Obj *srcObj, Tcl_Obj *dupObj)
{
    LrangeRep *repPtr = (LrangeRep *)srcObj->internalRep.twoPtrValue.ptr1;
    repPtr->refCount++;
    dupObj->internalRep.twoPtrValue.ptr1 = repPtr;
    dupObj->internalRep.twoPtrValue.ptr2 = NULL;
    dupObj->typePtr = srcObj->typePtr;
}

/* Implementation of Tcl_ObjType.lengthProc for lrangeType */
Tcl_Size
LrangeTypeLength(Tcl_Obj *objPtr)
{
    LrangeRep *repPtr = (LrangeRep *)objPtr->internalRep.twoPtrValue.ptr1;
    return repPtr->rangeLen;
}

/* Implementation of Tcl_ObjType.indexProc for lrangeType */
int
LrangeTypeIndex(
    Tcl_Interp *interp,
    Tcl_Obj *objPtr, /* Source list */
    Tcl_Size index,  /* Element index */
    Tcl_Obj **elemPtrPtr) /* Returned element */
{
    LrangeRep *repPtr = (LrangeRep *)objPtr->internalRep.twoPtrValue.ptr1;
    if (index < 0 || index >= repPtr->rangeLen) {
	*elemPtrPtr = NULL;
	return TCL_OK;
    }
    return Tcl_ListObjIndex(
	interp, repPtr->srcListPtr, repPtr->srcIndex + index, elemPtrPtr);
}

/* Implementation of Tcl_ObjType.sliceProc for lrangeType */
int
LrangeSlice(
    Tcl_Interp *interp,
    Tcl_Obj *objPtr, /* Source for the range */
    Tcl_Size start,  /* Start index */
    Tcl_Size end,    /* End index */
    Tcl_Obj **resultPtrPtr) /* Location to store result object */
{
    assert(objPtr->typePtr == &lrangeType);

    Tcl_Size rangeLen;
    LrangeRep *repPtr = (LrangeRep *)objPtr->internalRep.twoPtrValue.ptr1;
    Tcl_Obj *sourcePtr = repPtr->srcListPtr;

    rangeLen =
	TclNormalizeRangeLimits(&start, &end, repPtr->rangeLen);
    if (rangeLen == 0) {
	TclNewObj(*resultPtrPtr);
	return TCL_OK;
    }

    /*
     * Because of how ranges are constructed, they are never recursive.
     * Not that the code below cares...
     */
    assert(sourcePtr->typePtr != &lrangeType);

    Tcl_Size sourceLen;
    Tcl_Size newSrcIndex = start + repPtr->srcIndex;
    if (TclListObjLength(interp, sourcePtr, &sourceLen) != TCL_OK) {
	/* Cannot fail because how rangeType's are constructed but ... */
        return TCL_ERROR;
    }

    /*
     * At this point, sourcePtr is a non-lrangeType that will be the source
     * Tcl_Obj for the returned object. newSrcIndex is an index into this.
     */

    /*
     * A range is always smaller than its source thus the following must
     * hold even for recursive ranges.
     */
    assert((newSrcIndex + rangeLen) <= sourceLen);

    /*
     * We will only use the lrangeType abstract list if the following
     * conditions are met:
     *  1. The source list is not a non-abstract list since that has its
     *     own range operation with better performance and additional features.
     *  2. The length criteria for using rangeType are met.
     */
    if (sourcePtr->typePtr == &tclListType ||
	!LrangeMeetsLengthCriteria(rangeLen, sourceLen)) {
	/*
	 * Conditions not met, create non-abstract list.
	 * Note TclListObjRange will modify the sourcePtr in place if it is
	 * not shared (refCount <=1). We do not want that since our repPtr
	 * is holding a reference to it and it might be the only reference.
	 * Thus we must increment the refCount before calling TclListObjRange.
	 */

	Tcl_IncrRefCount(sourcePtr);
	*resultPtrPtr = TclListObjRange(
	    interp, sourcePtr, newSrcIndex, newSrcIndex + rangeLen - 1);
	assert(sourcePtr->refCount > 1);
	Tcl_DecrRefCount(sourcePtr);
	return *resultPtrPtr ? TCL_OK : TCL_ERROR;
    }

    if (!Tcl_IsShared(objPtr) && repPtr->refCount < 2) {
	/* Reuse this objPtr */
	repPtr->srcIndex = newSrcIndex;
	repPtr->rangeLen = rangeLen;
	Tcl_IncrRefCount(sourcePtr); /* Incr before decr ! */
	Tcl_DecrRefCount(repPtr->srcListPtr);
	repPtr->srcListPtr = sourcePtr;
	Tcl_InvalidateStringRep(objPtr);
	*resultPtrPtr = objPtr;
	return TCL_OK;
    }
    else {
	return LrangeNew(sourcePtr, newSrcIndex, rangeLen, resultPtrPtr);
    }
}

/*
 *------------------------------------------------------------------------
 *
 * Tcl_ListObjRange --
 *
 *    Returns a Tcl_Obj containing a list of elements from a given range
 *    in a source list.
 *
 * Results:
 *    Standard Tcl result.
 *
 * Side effects:
 *    Stores the result in *resultPtrPtr. This may be the same as objPtr,
 *    a new allocation, or a pointer to an internally stored object. In
 *    all cases, the reference count of the returned object is not
 *    incremented to account for the returned reference to it.
 *
 *------------------------------------------------------------------------
 */
int
Tcl_ListObjRange(
    Tcl_Interp *interp,
    Tcl_Obj *objPtr, /* Source for the range */
    Tcl_Size start,  /* Start index */
    Tcl_Size end,    /* End index */
    Tcl_Obj **resultPtrPtr) /* Location to store result object */
{
    int result;
    Tcl_Size srcLen;

    result = TclListObjLength(interp, objPtr, &srcLen);
    if (result != TCL_OK) {
	return result;
    }

    Tcl_Size rangeLen = TclNormalizeRangeLimits(&start, &end, srcLen);
    if (rangeLen == 0) {
	TclNewObj(*resultPtrPtr);
	return TCL_OK;
    }

    /*
     * If the list is an AbstractList with a specialized slice, use it.
     * Note this includes rangeType itself.
     */
    if (TclObjTypeHasProc(objPtr, sliceProc)) {
	return TclObjTypeSlice(interp, objPtr, start, end, resultPtrPtr);
    }

    /*
     * We will only use the lrangeType abstract list if the following
     * conditions are met:
     *  1. The source list is not a non-abstract list since that has its
     *     own range operation with better performance and additional features.
     *  2. The length criteria for using rangeType are met.
     */
    if (objPtr->typePtr == &tclListType ||
	!LrangeMeetsLengthCriteria(rangeLen, srcLen)) {
	/* Conditions not met, create non-abstract list */
	*resultPtrPtr = TclListObjRange(interp, objPtr, start, end);
	return *resultPtrPtr ? TCL_OK : TCL_ERROR;
    }

    /* Create a lrangeType referencing the original source list */
    return LrangeNew(objPtr, start, rangeLen, resultPtrPtr);
}