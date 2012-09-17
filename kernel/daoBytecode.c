/*
// Dao Virtual Machine
// http://www.daovm.net
//
// Copyright (c) 2006-2012, Limin Fu
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
// 
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
// SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include <string.h>
#include <assert.h>

#include "daoBytecode.h"
#include "daoOptimizer.h"
#include "daoNamespace.h"
#include "daoVmspace.h"
#include "daoValue.h"
#include "daoGC.h"


#define IntToPointer( x ) ((void*)(size_t)x)



#if 0
#define DEBUG_BC
#endif


enum DaoRoutineSubTypes
{
	DAO_ROUTINE_NORMAL ,
	DAO_ROUTINE_ABSTRACT ,
	DAO_ROUTINE_OVERLOADED 
};


enum DaoDeclarationTypes
{
	DAO_DECL_CREATE ,
	DAO_DECL_SEARCH ,
	DAO_DECL_LOADAS
};



DaoByteEncoder* DaoByteEncoder_New()
{
	DaoByteEncoder *self = (DaoByteEncoder*) dao_calloc( 1, sizeof(DaoByteEncoder) );

	self->header = DString_New(1);
	self->source = DString_New(1);
	self->modules = DString_New(1);
	self->identifiers = DString_New(1);
	self->declarations = DString_New(1);
	self->types = DString_New(1);
	self->values = DString_New(1);
	self->constants = DString_New(1);
	self->variables = DString_New(1);
	self->glbtypes = DString_New(1);
	self->interfaces = DString_New(1);
	self->classes = DString_New(1);
	self->routines = DString_New(1);

	self->tmpBytes = DString_New(1);
	self->valueBytes = DString_New(1);
	self->lookups = DArray_New(0);
	self->names = DArray_New(0);
	self->objects = DArray_New(0);
	self->hosts = DArray_New(0);
	self->handled = DHash_New(0,0);

	self->mapLookupHost = DHash_New(0,0);
	self->mapLookupName = DHash_New(0,D_STRING);

	self->mapIdentifiers = DHash_New(D_STRING,0);
	self->mapDeclarations = DHash_New(0,0);
	self->mapTypes = DHash_New(0,0);
	self->mapValues = DHash_New(0,0);
	self->mapValueBytes = DHash_New(D_STRING,0);
	self->mapInterfaces = DHash_New(0,0);
	self->mapClasses = DHash_New(0,0);
	self->mapRoutines = DHash_New(0,0);

	DString_AppendDataMBS( self->header, DAO_BC_SIGNATURE "\0", 9 );
	DString_AppendChar( self->header, sizeof(daoint) == 4 ? '\4' : '\x8' );
	DString_AppendDataMBS( self->header, "\0\0\0\0\0\0", 6 );
	return self;
}
void DaoByteEncoder_Delete( DaoByteEncoder *self )
{
	DString_Delete( self->header );
	DString_Delete( self->source );
	DString_Delete( self->modules );
	DString_Delete( self->identifiers );
	DString_Delete( self->declarations );
	DString_Delete( self->types );
	DString_Delete( self->values );
	DString_Delete( self->constants );
	DString_Delete( self->variables );
	DString_Delete( self->glbtypes );
	DString_Delete( self->interfaces );
	DString_Delete( self->classes );
	DString_Delete( self->routines );

	DString_Delete( self->tmpBytes );
	DString_Delete( self->valueBytes );
	DArray_Delete( self->lookups );
	DArray_Delete( self->names );
	DArray_Delete( self->objects );
	DArray_Delete( self->hosts );
	DMap_Delete( self->handled );

	DMap_Delete( self->mapLookupHost );
	DMap_Delete( self->mapLookupName );

	DMap_Delete( self->mapIdentifiers );
	DMap_Delete( self->mapDeclarations );
	DMap_Delete( self->mapTypes );
	DMap_Delete( self->mapValues );
	DMap_Delete( self->mapValueBytes );
	DMap_Delete( self->mapInterfaces );
	DMap_Delete( self->mapClasses );
	DMap_Delete( self->mapRoutines );
	dao_free( self );
}


static void DString_AppendString( DString *bytecodes, DString *string )
{
	DString_Append( bytecodes, string );
#ifdef DEBUG_BC
	DString_AppendChar( bytecodes, '\n' );
#endif
}
static void DString_AppendBytes( DString *bytecodes, const uchar_t *bytes, int n )
{
#ifdef DEBUG_BC
	const char* hexdigits = "0123456789ABCDEF";
	int i;
	for(i=0; i<n; ++i){
		DString_AppendChar( bytecodes, hexdigits[ bytes[i] >> 4 ] );
		DString_AppendChar( bytecodes, hexdigits[ bytes[i] & 0xF ] );
		DString_AppendChar( bytecodes, ' ' );
	}
	DString_AppendChar( bytecodes, '\n' );
	return;
#endif

	DString_AppendDataMBS( bytecodes, (char*) bytes, n );
}
static void DString_AppendUInt8( DString *bytecodes, int value )
{
	uchar_t bytes[2];
	bytes[0] = value & 0xFF;
	DString_AppendBytes( bytecodes, bytes, 1 );
}
static void DString_AppendUInt16( DString *bytecodes, int value )
{
	uchar_t bytes[2];
	bytes[0] = (value >> 8) & 0xFF;
	bytes[1] = value & 0xFF;
	DString_AppendBytes( bytecodes, bytes, 2 );
}
static void DString_AppendUInt32( DString *bytecodes, uint_t value )
{
	uchar_t bytes[4];
	bytes[0] = (value >> 24) & 0xFF;
	bytes[1] = (value >> 16) & 0xFF;
	bytes[2] = (value >>  8) & 0xFF;
	bytes[3] = value & 0xFF;
	DString_AppendBytes( bytecodes, bytes, 4 );
}
static void DString_AppendDaoInt( DString *bytecodes, daoint value )
{
	uchar_t i, bytes[8];
	uchar_t m = sizeof(daoint);
	for(i=0; i<m; ++i) bytes[i] = (value >> 8*(m-1-i)) & 0xFF;
	DString_AppendBytes( bytecodes, bytes, m );
}
/*
// IEEE 754 double-precision binary floating-point format:
//   sign(1)--exponent(11)------------fraction(52)---------------------
//   S EEEEEEEEEEE FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF  
//   63         52                                                    0
//
//   value = (-1)^S  *  ( 1 + \sigma_0^51 (b_i * 2^{-(52-i)}) )  *  2^{E-1023}
//
// Exponents 0x000 is used to represent zero (if F=0) and subnormals (if F!=0);
// Exponents 0x7FF is used to represent inf (if F=0) and NaNs (if F!=0);
// Where F is the fraction mantissa.
*/
static void DString_AppendNaN( DString *bytecodes )
{
	DString_AppendUInt32( bytecodes, 0x7FF << 20 );
	DString_AppendUInt32( bytecodes, 1 );
}
static void DString_AppendInf( DString *bytecodes )
{
	DString_AppendUInt32( bytecodes, 0x7FF << 20 );
	DString_AppendUInt32( bytecodes, 0 );
}
static void DString_AppendDouble( DString *bytecodes, double value )
{
	uint_t i = 20, m1 = 0, m2 = 0;
	int first = 1;
	int neg = value < 0.0;
	int expon = 0;
	double frac;

	if( value == 0.0 ){
		DString_AppendUInt32( bytecodes, 0 );
		DString_AppendUInt32( bytecodes, 0 );
		return;
	}else if( isnan( value ) ){
		DString_AppendNaN( bytecodes );
		return;
	}else if( isinf( value ) ){
		DString_AppendInf( bytecodes );
		return;
	}

	frac = frexp( fabs( value ), & expon );
	frac = 2.0 * frac - 1.0;
	expon -= 1;
	while(1){
		double prod = frac * 2.0;
		uint_t bit = (uint_t) prod;
		frac = prod - bit;
		i -= 1;
		if( first ){
			m1 |= bit << i;
			if( i == 0 ){
				first = 0;
				i = 32;
			}
		}else{
			m2 |= bit << i;
			if( i == 0 ) break;
		}
		if( frac <= 0.0 ) break;
	}
	m1 |= (expon+1023) << 20;
	if( neg ) m1 |= 1 << 31;
	DString_AppendUInt32( bytecodes, m1 );
	DString_AppendUInt32( bytecodes, m2 );
}
static void DString_AppendComplex( DString *bytecodes, complex16 value )
{
	DString_AppendDouble( bytecodes, value.real );
	DString_AppendDouble( bytecodes, value.imag );
}
int DaoByteEncoder_EncodeIdentifier( DaoByteEncoder *self, DString *name )
{
	DNode *node;
	if( name == NULL || name->mbs == NULL ) return 0;
	node = DMap_Find( self->mapIdentifiers, name );
	if( node ) return (int) node->value.pInt;
	DMap_Insert( self->mapIdentifiers, name, IntToPointer( self->mapIdentifiers->size + 1 ) );
	DString_AppendUInt16( self->identifiers, name->size );
	DString_AppendString( self->identifiers, name );
	return self->mapIdentifiers->size;
}
void DaoByteEncoder_EncodeModules( DaoByteEncoder *self )
{
	int i;
	for(i=0; i<self->nspace->loadings->size; i+=2){
		DString *s1 = self->nspace->loadings->items.pString[i];
		DString *s2 = self->nspace->loadings->items.pString[i+1];
		int id1 = DaoByteEncoder_EncodeIdentifier( self, s1 );
		int id2 = s2->size ? DaoByteEncoder_EncodeIdentifier( self, s2 ) : 0;
		DString_AppendUInt16( self->modules, id1 );
		DString_AppendUInt16( self->modules, id2 );
		if( id2 ){
			int size = self->mapDeclarations->size;
			int lk = DaoNamespace_FindConst( self->nspace, s2 );
			DaoValue *value = DaoNamespace_GetConst( self->nspace, lk );
			DNode *node;
			if( value == NULL || value->type != DAO_NAMESPACE ) break;
			if( DMap_Find( self->mapDeclarations, value ) ) break;
			DMap_Insert( self->mapDeclarations, value, IntToPointer( size+1 ) );
			DString_AppendUInt8( self->declarations, DAO_NAMESPACE );
			DString_AppendUInt8( self->declarations, DAO_DECL_LOADAS );
			DString_AppendUInt8( self->declarations, 0 );
			DString_AppendUInt16( self->declarations, 0 );
			DString_AppendUInt16( self->declarations, (i/2)+1 );
			DString_AppendUInt16( self->declarations, 0 );
		}
	}
}

void DaoByteEncoder_AddLookupValue( DaoByteEncoder *self, DaoValue *value, DaoValue *host, DString *name )
{
	if( DMap_Find( self->handled, value ) ) return;
	printf( "DaoByteEncoder_AddLookupValue: %p %p\n", value, host );
	switch( value->type ){
	case DAO_NAMESPACE :
	case DAO_INTERFACE :
	case DAO_CLASS :
	case DAO_CTYPE :
	case DAO_OBJECT :
	case DAO_CDATA :
	case DAO_ROUTINE :
		DArray_Append( self->hosts, value );
		DMap_Insert( self->handled, value, NULL );
		DMap_Insert( self->mapLookupHost, value, host );
		DMap_Insert( self->mapLookupName, value, name );
		break;
	}
}
void DaoByteEncoder_AddCfieldsLookup( DaoByteEncoder *self, DaoValue *host, DaoType *type )
{
	DString name = DString_WrapMBS( "" );
	DaoTypeKernel *kernel;
	DaoValue *value;
	DNode *it;
	DaoType_FindValue( type, & name );
	kernel = type->kernel;
	printf( "@@@@@@@@@@@@ DaoByteEncoder_AddCfieldsLookup: %s\n", type->name->mbs );
	if( kernel == NULL ) return;
	for(it=DMap_First(kernel->values); it; it=DMap_Next(kernel->values,it)){
		value = it->value.pValue;
		DaoByteEncoder_AddLookupValue( self, value, host, it->key.pString );
	}
	for(it=DMap_First(kernel->methods); it; it=DMap_Next(kernel->methods,it)){
		value = it->value.pValue;
		DaoByteEncoder_AddLookupValue( self, value, host, it->key.pString );
	}
}
void DaoByteEncoder_SetupLookupData( DaoByteEncoder *self )
{
	DaoNamespace *ns;
	DaoInterface *inter;
	DaoClass *klass;
	DaoCtype *ctype;
	DaoType *type;
	DNode *it;
	daoint i;

	if( self->nspace == NULL ) return;
	DArray_Append( self->hosts, self->nspace );
	DMap_Insert( self->handled, self->nspace, NULL );
	for(i=0; i<self->hosts->size; ++i){
		DaoValue *value, *host = self->hosts->items.pValue[i];
		switch( host->type ){
		case DAO_NAMESPACE :
			ns = (DaoNamespace*) host;
			for(it=DMap_First(ns->lookupTable); it; it=DMap_Next(ns->lookupTable,it)){
				int st = LOOKUP_ST( it->value.pInt );
				int id = LOOKUP_ID( it->value.pInt );
				if( st != DAO_GLOBAL_CONSTANT ) continue;
				value = ns->constants->items.pConst[id]->value;
				DaoByteEncoder_AddLookupValue( self, value, host, it->key.pString );
			}
			break;
		case DAO_CLASS :
			klass = (DaoClass*) host;
			for(it=DMap_First(klass->lookupTable); it; it=DMap_Next(klass->lookupTable,it)){
				int st = LOOKUP_ST( it->value.pInt );
				int id = LOOKUP_ID( it->value.pInt );
				if( st != DAO_CLASS_CONSTANT ) continue;
				value = klass->constants->items.pConst[id]->value;
				DaoByteEncoder_AddLookupValue( self, value, host, it->key.pString );
			}
			break;
		case DAO_CTYPE :
			ctype = (DaoCtype*) host;
			DaoByteEncoder_AddCfieldsLookup( self, host, ctype->ctype );
			break;
		case DAO_INTERFACE :
			inter = (DaoInterface*) host;
			for(it=DMap_First(inter->methods); it; it=DMap_Next(inter->methods,it)){
				value = it->value.pValue;
				DaoByteEncoder_AddLookupValue( self, value, host, it->key.pString );
			}
			break;
		default :
			type = DaoNamespace_GetType( self->nspace, host );
			DaoByteEncoder_AddCfieldsLookup( self, host, type );
			break;
		}
	}
}

int DaoByteEncoder_EncodeType( DaoByteEncoder *self, DaoType *type );

int DaoByteEncoder_FindDeclaration( DaoByteEncoder *self, DaoValue *object )
{
	DNode *node = DMap_Find( self->mapDeclarations, object );
	if( node ) return node->value.pInt;
	return 0;
}

int DaoByteEncoder_EncodeDeclaration( DaoByteEncoder *self, DaoValue *object )
{
	DaoClass *klass;
	DaoRoutine *routine;
	DaoType *type = NULL;
	DString *name = NULL;
	DNode *node, *node2;
	int nameid, nameid2 = 0, hostid = 0;
	int dectype = DAO_DECL_SEARCH;
	int subtype = 0;

	if( object == NULL || object == (DaoValue*) self->nspace ) return 0;

	node = DMap_Find( self->mapDeclarations, object );
	if( node ) return node->value.pInt;

	node = DMap_Find( self->mapLookupHost, object );
	if( node ){
		node2 = DMap_Find( self->mapLookupName, object );
		hostid = DaoByteEncoder_EncodeDeclaration( self, node->value.pValue );
		nameid2 = DaoByteEncoder_EncodeIdentifier( self, node2->value.pString );
	}

	switch( object->type ){
	case DAO_CLASS :
		klass = (DaoClass*) object;
		type = klass->objType;
		name = object->xClass.className;
		if( klass->classRoutine->nameSpace == self->nspace ){
			DArray_Append( self->objects, object );
			dectype = DAO_DECL_CREATE;
		}
		break;
	case DAO_CTYPE :
		type = object->xCtype.ctype;
		name = type->name;
		break;
	case DAO_INTERFACE :
		type = object->xInterface.abtype;
		name = type->name;
		if( object->xInterface.nspace == self->nspace ){
			DArray_Append( self->objects, object );
			dectype = DAO_DECL_CREATE;
		}
		break;
	case DAO_ROUTINE :
		routine = (DaoRoutine*) object;
		name = routine->routName;
		subtype = DAO_ROUTINE_NORMAL;
		if( routine->nameSpace == self->nspace && routine->pFunc == NULL ){
			DArray_Append( self->objects, object );
			dectype = DAO_DECL_CREATE;
			if( routine->overloads ){
				subtype = DAO_ROUTINE_OVERLOADED;
			}else if( routine->body == NULL ){
				subtype = DAO_ROUTINE_ABSTRACT;
			}
		}
		break;
	case DAO_NAMESPACE :
		break;
	default : return 0;
	}
	if( name ) printf( "@@@@@@@@@@@@@@@ %s %i %i %p %p\n", name->mbs, hostid, nameid2, node, object );

	DMap_Insert( self->mapDeclarations, object, IntToPointer( self->mapDeclarations->size+1 ) );
	nameid = DaoByteEncoder_EncodeIdentifier( self, name );
	DString_AppendUInt8( self->declarations, object->type );
	DString_AppendUInt8( self->declarations, subtype );
	DString_AppendUInt8( self->declarations, dectype );
	DString_AppendUInt16( self->declarations, nameid );
	DString_AppendUInt16( self->declarations, hostid );
	DString_AppendUInt16( self->declarations, nameid2 );

	if( type ) DaoByteEncoder_EncodeType( self, type );
	return self->mapDeclarations->size;
}
int DaoByteEncoder_FindType( DaoByteEncoder *self, DaoType *type )
{
	DNode *node = DMap_Find( self->mapTypes, type );
	if( node ) return node->value.pInt;
	return 0;
}
int DaoByteEncoder_EncodeSimpleType( DaoByteEncoder *self, DaoType *type )
{
	int nameid = DaoByteEncoder_EncodeIdentifier( self, type->name );
	DString_AppendUInt8( self->types, type->tid );
	DString_AppendUInt8( self->types, type->attrib );
	DString_AppendUInt16( self->types, nameid );
	DMap_Insert( self->mapTypes, type, IntToPointer( self->mapTypes->size + 1 ) );
	return self->mapTypes->size;
}
int DaoByteEncoder_EncodeAliasType( DaoByteEncoder *self, DaoType *type, int tid )
{
	int nameid = DaoByteEncoder_EncodeIdentifier( self, type->name );
	DString_AppendUInt8( self->types, 0xFF );
	DString_AppendUInt16( self->types, nameid );
	DString_AppendUInt16( self->types, tid );
	DMap_Insert( self->mapTypes, type, IntToPointer( self->mapTypes->size + 1 ) );
	return self->mapTypes->size;
}

int DaoByteEncoder_EncodeValue( DaoByteEncoder *self, DaoValue *value );

int DaoByteEncoder_EncodeType( DaoByteEncoder *self, DaoType *type )
{
	DNode *node;
	int i, k, n, tpid, dec, cb;
	int nameid = 0;
	int typeid = 0;
	int valueid = 0;

	if( type == NULL ) return 0;
	node = DMap_Find( self->mapTypes, type );
	if( node ) return node->value.pInt;
	if( strcmp( type->name->mbs, "list<string>" ) == 0 )
		printf( "~~~~~~~~~~~~~~~~~~~~~~~ list<string>  %p  %p\n", type, self->nspace );

	switch( type->tid ){
	case DAO_NONE :
	case DAO_INTEGER : case DAO_FLOAT : case DAO_DOUBLE :
	case DAO_COMPLEX : case DAO_LONG  : case DAO_STRING :
		return DaoByteEncoder_EncodeSimpleType( self, type );
	case DAO_ENUM :
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		DString_AppendUInt8( self->types, type->flagtype );
		if( type->mapNames ){
			DString_AppendUInt16( self->types, type->mapNames->size );
			for(node=DMap_First(type->mapNames); node; node=DMap_Next(type->mapNames,node)){
				int nameid = DaoByteEncoder_EncodeIdentifier( self, node->key.pString );
				DString_AppendUInt16( self->types, nameid );
				DString_AppendUInt32( self->types, node->value.pInt );
			}
		}else{
			DString_AppendUInt16( self->types, 0 );
		}
		break;
	case DAO_ARRAY : case DAO_LIST : case DAO_MAP :
	case DAO_TUPLE : case DAO_TYPE : case DAO_FUTURE :
	case DAO_VARIANT : 
		if( type->tid == DAO_VARIANT && type->aux ){
			DaoByteEncoder_EncodeType( self, (DaoType*) type->aux );
		}
		for(i=0,n=type->nested->size; i<n; ++i){
			int id = DaoByteEncoder_EncodeType( self, type->nested->items.pType[i] );
		}
		if( (typeid = DaoByteEncoder_FindType( self, type )) ) break;
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		if( type->tid == DAO_VARIANT ){
			int id = DaoByteEncoder_EncodeType( self, (DaoType*) type->aux );
			DString_AppendUInt16( self->types, id );
		}
		DString_AppendUInt16( self->types, type->nested->size );
		for(i=0,n=type->nested->size; i<n; ++i){
			int id = DaoByteEncoder_EncodeType( self, type->nested->items.pType[i] );
			DString_AppendUInt16( self->types, id );
		}
		break;
	case DAO_VALTYPE :
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		valueid = DaoByteEncoder_EncodeValue( self, type->aux );
		DString_AppendUInt32( self->types, valueid );
		break;
	case DAO_PAR_NAMED :
	case DAO_PAR_DEFAULT :
		tpid = DaoByteEncoder_EncodeType( self, (DaoType*) type->aux );
		if( (typeid = DaoByteEncoder_FindType( self, type )) ) break;
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		nameid = DaoByteEncoder_EncodeIdentifier( self, type->fname );
		DString_AppendUInt16( self->types, nameid );
		DString_AppendUInt16( self->types, tpid );
		break;
	case DAO_PAR_VALIST :
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		break;
	case DAO_CODEBLOCK :
		tpid = DaoByteEncoder_EncodeType( self, (DaoType*) type->aux );
		for(i=0,n=type->nested->size; i<n; ++i){
			int id = DaoByteEncoder_EncodeType( self, type->nested->items.pType[i] );
		}
		if( (typeid = DaoByteEncoder_FindType( self, type )) ) break;
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		DString_AppendUInt16( self->types, tpid );
		DString_AppendUInt16( self->types, type->nested->size );
		for(i=0,n=type->nested->size; i<n; ++i){
			int id = DaoByteEncoder_EncodeType( self, type->nested->items.pType[i] );
			DString_AppendUInt16( self->types, id );
		}
		break;
	case DAO_ROUTINE :
		dec = tpid = 0;
		if( type->aux->type == DAO_ROUTINE ){
			dec = DaoByteEncoder_EncodeDeclaration( self, type->aux );
		}else{
			tpid = DaoByteEncoder_EncodeType( self, (DaoType*) type->aux );
		}
		cb = DaoByteEncoder_EncodeType( self, type->cbtype );
		for(i=0,n=type->nested->size; i<n; ++i){
			int id = DaoByteEncoder_EncodeType( self, type->nested->items.pType[i] );
		}
		if( (typeid = DaoByteEncoder_FindType( self, type )) ) break;
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		DString_AppendUInt16( self->types, dec );
		DString_AppendUInt16( self->types, tpid );
		DString_AppendUInt16( self->types, cb );
		DString_AppendUInt16( self->types, type->nested->size );
		for(i=0,n=type->nested->size; i<n; ++i){
			int id = DaoByteEncoder_EncodeType( self, type->nested->items.pType[i] );
			DString_AppendUInt16( self->types, id );
		}
		break;
	case DAO_OBJECT :
	case DAO_CDATA :
	case DAO_CLASS :
	case DAO_CTYPE :
	case DAO_INTERFACE :
		k = DaoByteEncoder_EncodeDeclaration( self, type->aux );
		if( (typeid = DaoByteEncoder_FindType( self, type )) ) break;
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		DString_AppendUInt16( self->types, k );
		break;
	case DAO_ANY :
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		break;
	case DAO_THT :
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		break;
	default :
		typeid = DaoByteEncoder_EncodeSimpleType( self, type );
		break;
	}
	return typeid;
}
int DaoByteEncoder_EncodeType2( DaoByteEncoder *self, DaoType *type, DString *alias )
{
	int typeid = DaoByteEncoder_EncodeType( self, type );
	if( alias == NULL || DString_EQ( type->name, alias ) ) return typeid;
	return DaoByteEncoder_EncodeAliasType( self, type, typeid );
}
void DaoByteEncoder_EncodeValue2( DaoByteEncoder *self, DaoValue *value )
{
	DNode *it;
	DaoValue *va;
	DaoArray *array;
	DaoObject *object;
	DString *string = DString_New(1);
	DString *valueBytes = self->valueBytes;
	daoint i, n, id, id2, typeid, valueid;

	self->valueBytes->size = 0;
	switch( value->type ){
	case DAO_NONE :
		DString_AppendUInt8( valueBytes, value->type );
		break;
	case DAO_INTEGER :
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendDaoInt( valueBytes, value->xInteger.value );
		break;
	case DAO_FLOAT :
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendDouble( valueBytes, value->xFloat.value );
		break;
	case DAO_DOUBLE :
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendDouble( valueBytes, value->xDouble.value );
		break;
	case DAO_COMPLEX :
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendComplex( valueBytes, value->xComplex.value );
		break;
	case DAO_LONG :
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt8( valueBytes, value->xLong.value->sign < 0 );
		DString_AppendDaoInt( valueBytes, value->xLong.value->size );
		for(i=0; i<value->xLong.value->size; ++i){
			DString_AppendUInt8( valueBytes, value->xLong.value->data[i] );
		}
		break;
	case DAO_STRING :
		DString_AppendUInt8( valueBytes, value->type );
		if( value->xString.data->mbs ){
			DString_AppendUInt8( valueBytes, (1<<4)|1 );
			DString_AppendDaoInt( valueBytes, value->xString.data->size );
			DString_AppendString( valueBytes, value->xString.data );
		}else{
			uint_t max = 0;
			for(i=0; i<value->xString.data->size; ++i){
				wchar_t ch = value->xString.data->wcs[i];
				if( ch > max ) max = ch;
			}
			for(i=1; i<=4; ++i){
				max = max >> 8;
				if( max == 0 ) break;
			}
			if( i == 3 ) i = 4;
			max = i;
			DString_AppendUInt8( valueBytes, (1<<4)|max );
			DString_AppendDaoInt( valueBytes, value->xString.data->size );
			for(i=0; i<value->xString.data->size; ++i){
				wchar_t ch = value->xString.data->wcs[i];
				switch( max ){
				case 1 : DString_AppendUInt8( valueBytes, ch );  break;
				case 2 : DString_AppendUInt16( valueBytes, ch ); break;
				case 4 : DString_AppendUInt32( valueBytes, ch ); break;
				}
			}
		}
		break;
	case DAO_ENUM :
		typeid = DaoByteEncoder_EncodeType( self, value->xEnum.etype );
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt16( valueBytes, typeid );
		DString_AppendUInt32( valueBytes, value->xEnum.value );
		break;
	case DAO_ARRAY :
		array = (DaoArray*) value;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt8( valueBytes, array->etype );
		DString_AppendDaoInt( valueBytes, array->size );
		DString_AppendUInt16( valueBytes, array->ndim );
		for(i=0; i<array->ndim; ++i) DString_AppendDaoInt( valueBytes, array->dims[i] );
		for(i=0; i<array->size; ++i){
			switch( array->etype ){
			case DAO_INTEGER : DString_AppendDaoInt( valueBytes, array->data.i[i] ); break;
			case DAO_FLOAT   : DString_AppendDouble( valueBytes, array->data.f[i] ); break;
			case DAO_DOUBLE  : DString_AppendDouble( valueBytes, array->data.d[i] ); break;
			case DAO_COMPLEX : DString_AppendComplex( valueBytes, array->data.c[i] ); break;
			}
		}
		break;
	case DAO_LIST :
		for(i=0; i<value->xList.items.size; ++i){
			valueid = DaoByteEncoder_EncodeValue( self, value->xList.items.items.pValue[i] );
			DString_AppendUInt32( string, valueid );
		}
		typeid = DaoByteEncoder_EncodeType( self, value->xList.unitype );
		self->valueBytes->size = 0;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt16( valueBytes, typeid );
		DString_AppendDaoInt( valueBytes, value->xList.items.size );
		DString_Append( valueBytes, string );
		break;
	case DAO_MAP :
		for(it=DMap_First(value->xMap.items); it; it=DMap_Next(value->xMap.items,it) ){
			int ik = DaoByteEncoder_EncodeValue( self, it->key.pValue );
			int iv = DaoByteEncoder_EncodeValue( self, it->value.pValue );
			DString_AppendUInt32( string, ik );
			DString_AppendUInt32( string, iv );
		}
		typeid = DaoByteEncoder_EncodeType( self, value->xMap.unitype );
		self->valueBytes->size = 0;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt32( valueBytes, value->xMap.items->hashing );
		DString_AppendUInt16( valueBytes, typeid );
		DString_AppendDaoInt( valueBytes, value->xMap.items->size );
		DString_Append( valueBytes, string );
		break;
	case DAO_TUPLE :
		for(i=0; i<value->xTuple.size; ++i){
			valueid = DaoByteEncoder_EncodeValue( self, value->xTuple.items[i] );
			DString_AppendUInt32( string, valueid );
		}
		typeid = DaoByteEncoder_EncodeType( self, value->xTuple.unitype );
		self->valueBytes->size = 0;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt16( valueBytes, typeid );
		DString_AppendUInt32( valueBytes, value->xTuple.size );
		DString_Append( valueBytes, string );
		break;
	case DAO_OBJECT :
		object = (DaoObject*) value;
		va = (DaoValue*) object->defClass;
		id = DaoByteEncoder_EncodeDeclaration( self, va );
		if( value == object->defClass->objType->value ){
			DString_AppendUInt8( valueBytes, value->type );
			DString_AppendUInt8( valueBytes, 0x2 );
			DString_AppendUInt16( valueBytes, id );
			break;
		}
		if( object->rootObject == object ){ /* isRoot??? */
			for(i=0; i<object->valueCount; ++i){
				valueid = DaoByteEncoder_EncodeValue( self, object->objValues[i] );
				DString_AppendUInt32( string, valueid );
			}
		}else{
			valueid = DaoByteEncoder_EncodeValue( self, (DaoValue*) object->rootObject );
			DString_AppendUInt32( string, valueid );
		}
		self->valueBytes->size = 0;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt8( valueBytes, object->rootObject != object );
		DString_AppendUInt16( valueBytes, id );
		DString_AppendDaoInt( valueBytes, value->xTuple.size );
		DString_Append( valueBytes, string );
		break;
	case DAO_ROUTINE :
		id = DaoByteEncoder_EncodeDeclaration( self, value );
		typeid = DaoByteEncoder_EncodeType( self, value->xRoutine.routType );
		id2 = DaoByteEncoder_EncodeType( self, value->xRoutine.routHost );
		self->valueBytes->size = 0;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt16( valueBytes, id );
		DString_AppendUInt16( valueBytes, typeid );
		DString_AppendUInt16( valueBytes, id2 );
		DString_AppendUInt8( valueBytes, value->xRoutine.attribs );
		break;
	case DAO_CLASS :
	case DAO_CTYPE :
	case DAO_INTERFACE :
	case DAO_NAMESPACE : // XXX
		id = DaoByteEncoder_EncodeDeclaration( self, value );
		self->valueBytes->size = 0;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt16( valueBytes, id );
		break;
	case DAO_TYPE :
		typeid = DaoByteEncoder_EncodeType( self, (DaoType*) value );
		self->valueBytes->size = 0;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt16( valueBytes, typeid );
		break;
	case DAO_PAR_NAMED :
		id = DaoByteEncoder_EncodeIdentifier( self, value->xNameValue.name );
		typeid = DaoByteEncoder_EncodeType( self, (DaoType*) value->xNameValue.unitype );
		valueid = DaoByteEncoder_EncodeValue( self, value->xNameValue.value );
		printf( "~~~~~~~~~~~~~~~~~~~~~~~~ valueid = %i; %i %i %f\n", valueid, typeid, id, value->xNameValue.value->xDouble.value );
		self->valueBytes->size = 0;
		DString_AppendUInt8( valueBytes, value->type );
		DString_AppendUInt16( valueBytes, id );
		DString_AppendUInt16( valueBytes, typeid );
		DString_AppendUInt32( valueBytes, valueid );
		break;
	}
	DString_Delete( string );
}
int DaoByteEncoder_EncodeValue( DaoByteEncoder *self, DaoValue *value )
{
	DNode *node;
	if( value == NULL ) return 0;
	node = DMap_Find( self->mapValues, value );
	if( node ) return node->value.pInt;

	/* Insert first avoid loop recursion: */
	DMap_Insert( self->mapValues, value, IntToPointer( self->valueCount ) );

	DaoByteEncoder_EncodeValue2( self, value );
	node = DMap_Find( self->mapValueBytes, self->valueBytes );
	if( node ){
		DMap_Insert( self->mapValues, value, node->value.pVoid );
		return node->value.pInt;
	}
	assert( self->valueBytes->size > 0 );
	self->valueCount += 1;
	DString_Append( self->values, self->valueBytes );
	DMap_Insert( self->mapValues, value, IntToPointer( self->valueCount ) );
	DMap_Insert( self->mapValueBytes, self->valueBytes, IntToPointer( self->valueCount ) );
	self->valueBytes->size = 0;
	return self->valueCount;
}

void DaoByteEncoder_EncodeConstant( DaoByteEncoder *self, DString *name, DaoValue *value, int id, int pm )
{
	int nameid = DaoByteEncoder_EncodeIdentifier( self, name );
	int valueid = DaoByteEncoder_EncodeValue( self, value );
	self->valueBytes->size = 0;
	DString_AppendUInt16( self->valueBytes, nameid );
	DString_AppendUInt16( self->valueBytes, id );
	DString_AppendUInt8( self->valueBytes, pm );
	DString_AppendUInt32( self->valueBytes, valueid );
}

void DaoByteEncoder_EncodeVariable( DaoByteEncoder *self, DString *name, DaoVariable *var, int id, int pm )
{
	/* var->value maybe NULL or none value for uninitialized variable: */
	DaoValue *value = DaoType_MatchValue( var->dtype, var->value, NULL ) ? var->value : NULL;
	int nameid = DaoByteEncoder_EncodeIdentifier( self, name );
	int typeid = DaoByteEncoder_EncodeType( self, var->dtype );
	int valueid = DaoByteEncoder_EncodeValue( self, value );
	self->valueBytes->size = 0;
	printf( "%6i: %6i\n", id, valueid );
	DString_AppendUInt16( self->valueBytes, nameid );
	DString_AppendUInt16( self->valueBytes, id );
	DString_AppendUInt8( self->valueBytes, pm );
	DString_AppendUInt16( self->valueBytes, typeid );
	DString_AppendUInt32( self->valueBytes, valueid );
}

void DaoByteEncoder_GetLookupName( int size, DMap *lookupTable, DArray *lookups, DArray *names, int st )
{
	DNode *it;

	lookups->size = 0;
	names->size = 0;
	DArray_Resize( lookups, size, IntToPointer(LOOKUP_BIND(st,0,0,0)) );
	DArray_Resize( names, size, NULL );
	for(it=DMap_First(lookupTable); it; it=DMap_Next(lookupTable,it)){
		int id = LOOKUP_ID( it->value.pInt );
		if( LOOKUP_ST( it->value.pInt ) != st ) continue;
		lookups->items.pInt[id] = it->value.pInt;
		names->items.pString[id] = it->key.pString;
	}
}

void DaoByteEncoder_EncodeInterface( DaoByteEncoder *self, DaoInterface *interface )
{
	DNode *it;
	int i, id, count = 0;;

	if( DMap_Find( self->mapInterfaces, interface ) ) return;
	DMap_Insert( self->mapInterfaces, interface, IntToPointer( self->mapInterfaces->size+1 ) );

	id = DaoByteEncoder_FindDeclaration( self, (DaoValue*)interface );
	assert( id > 0 );

	DString_AppendUInt16( self->interfaces, id );
	DString_AppendUInt16( self->interfaces, interface->supers->size );
	for(i=0; i<interface->supers->size; ++i){
		DaoValue *value = interface->supers->items.pValue[i];
		id = DaoByteEncoder_EncodeDeclaration( self, value );
		DString_AppendUInt16( self->interfaces, id );
	}
	for(it=DMap_First(interface->methods); it; it=DMap_Next(interface->methods,it)){
		DaoRoutine *routine = it->value.pRoutine;
		count += routine->overloads ? routine->overloads->routines->size : 1;
	}
	DString_AppendUInt16( self->interfaces, count );
	for(it=DMap_First(interface->methods); it; it=DMap_Next(interface->methods,it)){
		DaoRoutine *routine = it->value.pRoutine;
		if( routine->overloads ){
			for(i=0, count=routine->overloads->routines->size; i<count; ++i){
				DaoValue *rout = routine->overloads->routines->items.pValue[i];
				int routid = DaoByteEncoder_EncodeValue( self, rout );
				DString_AppendUInt32( self->interfaces, routid );
			}
		}else{
			int routid = DaoByteEncoder_EncodeValue( self, it->value.pValue );
			DString_AppendUInt32( self->interfaces, routid );
		}
	}
}

void DaoByteEncoder_EncodeClass( DaoByteEncoder *self, DaoClass *klass )
{
	DNode *it;
	DMap *abstypes;
	int i, id, id2, count;

	if( DMap_Find( self->mapClasses, klass ) ) return;
	DMap_Insert( self->mapClasses, klass, IntToPointer( self->mapClasses->size + 1 ) );

	printf( "===============Encoding %s================\n", klass->className->mbs );

	id = DaoByteEncoder_FindDeclaration( self, (DaoValue*)klass );
	id2 = DaoByteEncoder_EncodeDeclaration( self, (DaoValue*)klass->classRoutine );
	assert( id > 0 );

	DaoByteEncoder_EncodeValue( self, (DaoValue*)klass->classRoutine );
	DString_AppendUInt16( self->classes, id );
	DString_AppendUInt16( self->classes, id2 );
	DString_AppendUInt16( self->classes, klass->attribs );
	DString_AppendUInt16( self->classes, klass->superClass->size );
	for(i=0; i<klass->superClass->size; ++i){
		DaoValue *value = klass->superClass->items.pValue[i];
		id = DaoByteEncoder_EncodeDeclaration( self, value );
		DString_AppendUInt16( self->classes, id );
	}

	count = 0;
	self->tmpBytes->size = 0;
	for(i=0; i<klass->cstDataName->size; ++i){
		DaoValue *value = klass->constants->items.pConst[i]->value;
		DString *name = klass->cstDataName->items.pString[i];
		DNode *node = MAP_Find( klass->lookupTable, name );
		if( LOOKUP_UP( node->value.pInt ) ) continue;
		id = LOOKUP_ID( node->value.pInt );
		DaoByteEncoder_EncodeDeclaration( self, value );
		DaoByteEncoder_EncodeConstant( self, name, value, id, LOOKUP_PM( node->value.pInt ) );
		DString_Append( self->tmpBytes, self->valueBytes );
		count += 1;
	}
	DString_AppendUInt16( self->classes, count );
	DString_Append( self->classes, self->tmpBytes );

	count = 0;
	self->tmpBytes->size = 0;
	for(i=0; i<klass->glbDataName->size; ++i){
		DaoVariable *var = klass->variables->items.pVar[i];
		DString *name = klass->glbDataName->items.pString[i];
		DNode *node = MAP_Find( klass->lookupTable, name );
		int pm = LOOKUP_PM( node->value.pInt );
		if( LOOKUP_UP( node->value.pInt ) ) continue;
		id = LOOKUP_ID( node->value.pInt );
		DaoByteEncoder_EncodeDeclaration( self, var->value );
		DaoByteEncoder_EncodeVariable( self, name, var, id, pm );
		DString_Append( self->tmpBytes, self->valueBytes );
		count += 1;
	}
	DString_AppendUInt16( self->classes, count );
	DString_Append( self->classes, self->tmpBytes );

	count = 0;
	self->tmpBytes->size = 0;
	for(i=0; i<klass->objDataName->size; ++i){
		DaoVariable *var = klass->instvars->items.pVar[i];
		DString *name = klass->objDataName->items.pString[i];
		DNode *node = MAP_Find( klass->lookupTable, name );
		int pm = LOOKUP_PM( node->value.pInt );
		if( LOOKUP_UP( node->value.pInt ) ) continue;
		id = LOOKUP_ID( node->value.pInt );
		if( id < i ) break; /* self from parent class; */
		DaoByteEncoder_EncodeDeclaration( self, var->value );
		DaoByteEncoder_EncodeVariable( self, name, var, id, pm );
		DString_Append( self->tmpBytes, self->valueBytes );
		count += 1;
	}
	DString_AppendUInt16( self->classes, count );
	DString_Append( self->classes, self->tmpBytes );

	abstypes = klass->abstypes;
	DString_AppendUInt16( self->classes, abstypes->size );
	for(it=DMap_First(abstypes); it; it=DMap_Next(abstypes,it)){
		int nameid = DaoByteEncoder_EncodeIdentifier( self, it->key.pString );
		int typeid = DaoByteEncoder_EncodeType( self, it->value.pType );
		DString_AppendUInt16( self->classes, nameid );
		DString_AppendUInt16( self->classes, typeid );
	}
}

void DaoByteEncoder_EncodeRoutine( DaoByteEncoder *self, DaoRoutine *routine )
{
	DNode *it;
	DMap *abstypes;
	DaoType *type;
	int i, id, id2, id3;

	if( routine->body == NULL ) return;
	if( DMap_Find( self->mapRoutines, routine ) ) return;
	DMap_Insert( self->mapRoutines, routine, IntToPointer( self->mapRoutines->size + 1 ) );

	printf( ">>>>>>>>>>>>>>>>>>>>> encode routine: %p %s %s %i\n", routine, routine->routName->mbs, routine->routType->name->mbs, (int)self->mapRoutines->size );

	id = DaoByteEncoder_FindDeclaration( self, (DaoValue*)routine );
	assert( id > 0 );

#ifdef DEBUG_BC
	DString_Append( self->routines, routine->routName );
	DString_AppendMBS( self->routines, "()====\n" );
#endif

	DString_AppendUInt16( self->routines, id );
	id = DaoByteEncoder_EncodeIdentifier( self, routine->routName );
	id2 = DaoByteEncoder_EncodeType( self, routine->routType );
	id3 = DaoByteEncoder_EncodeType( self, routine->routHost );
	DString_AppendUInt16( self->routines, id );
	DString_AppendUInt16( self->routines, id2 );
	DString_AppendUInt16( self->routines, id3 );
	DString_AppendUInt16( self->routines, routine->attribs );
	DString_AppendUInt16( self->routines, routine->defLine );

	abstypes = routine->body->abstypes;
	for(it=DMap_First(abstypes); it; it=DMap_Next(abstypes,it)){
		DaoByteEncoder_EncodeType( self, it->value.pType );
	}
	DString_AppendUInt16( self->routines, routine->routConsts->items.size );
	for(i=0; i<routine->routConsts->items.size; ++i){
		DaoValue *value = routine->routConsts->items.items.pValue[i];
		id = DaoByteEncoder_EncodeValue( self, value );
		DString_AppendUInt32( self->routines, id );
	}
	abstypes = routine->body->localVarType;
	DString_AppendUInt16( self->routines, routine->body->regType->size );
	DString_AppendUInt16( self->routines, abstypes->size );
	for(it=DMap_First(abstypes); it; it=DMap_Next(abstypes,it)){
		id = DaoByteEncoder_EncodeType( self, it->value.pType );
		DString_AppendUInt16( self->routines, it->key.pInt );
		DString_AppendUInt16( self->routines, id );
	}

#ifdef DEBUG_BC
	DString_AppendMBS( self->routines, "Codes----\n" );
#endif

	DString_AppendUInt16( self->routines, routine->body->annotCodes->size );
	for(i=0; i<routine->body->annotCodes->size; ++i){
		DaoVmCodeX *vmc = routine->body->annotCodes->items.pVmc[i];
		DString_AppendUInt16( self->routines, vmc->code );
		DString_AppendUInt16( self->routines, vmc->a );
		DString_AppendUInt16( self->routines, vmc->b );
		DString_AppendUInt16( self->routines, vmc->c );
		DString_AppendUInt16( self->routines, vmc->level );
		DString_AppendUInt16( self->routines, vmc->line );
	}
}

void DaoByteEncoder_EncodeNamespace( DaoByteEncoder *self, DaoNamespace *nspace )
{
	DNode *it;
	DMap *abstypes;
	DArray *constants = nspace->constants;
	DArray *variables = nspace->variables;
	DMap *lookupTable = nspace->lookupTable;
	daoint i, n, st, pm, up, id;

	DaoByteEncoder_SetupLookupData( self );
	DaoByteEncoder_EncodeModules( self );

	DaoByteEncoder_GetLookupName( constants->size, lookupTable, self->lookups, self->names, DAO_GLOBAL_CONSTANT );
	for(i=0, n=constants->size; i<n; ++i){
		DaoValue *value = constants->items.pConst[i]->value;
		DString *name = self->names->items.pString[i];
		int pm = LOOKUP_PM( self->lookups->items.pInt[i] );
		int up = LOOKUP_UP( self->lookups->items.pInt[i] );
		DaoByteEncoder_EncodeDeclaration( self, value );
		if( up ) continue;
		if( name == NULL && value->type == DAO_ROUTINE && value->xRoutine.nameSpace == self->nspace ){
			it = DMap_Find( lookupTable, value->xRoutine.routName );
			id = it ? LOOKUP_ID( it->value.pInt ) : i;
			/* name may be NULL for overloaded functions: */
			DaoByteEncoder_EncodeConstant( self, value->xRoutine.routName, value, id, pm );
		}else{
			DaoByteEncoder_EncodeConstant( self, name, value, i, pm );
		}
		DString_Append( self->constants, self->valueBytes );
		self->constCount += 1;
	}
	DaoByteEncoder_GetLookupName( variables->size, lookupTable, self->lookups, self->names, DAO_GLOBAL_VARIABLE );
	for(i=0, n=variables->size; i<n; ++i){
		DaoVariable *var = variables->items.pVar[i];
		DString *name = self->names->items.pString[i];
		int pm = LOOKUP_PM( self->lookups->items.pInt[i] );
		int up = LOOKUP_UP( self->lookups->items.pInt[i] );
		DaoByteEncoder_EncodeDeclaration( self, var->value );
		if( up ) continue;
		DaoByteEncoder_EncodeVariable( self, name, var, i, pm );
		DString_Append( self->variables, self->valueBytes );
		self->varCount += 1;
	}

	abstypes = nspace->abstypes;
	for(it=DMap_First(abstypes); it; it=DMap_Next(abstypes,it)){
		int nameid = DaoByteEncoder_EncodeIdentifier( self, it->key.pString );
		int typeid = DaoByteEncoder_EncodeType( self, it->value.pType );
		DString_AppendUInt16( self->glbtypes, nameid );
		DString_AppendUInt16( self->glbtypes, typeid );
	}
	for(i=0; i<self->objects->size; ++i){
		DaoValue *value = self->objects->items.pValue[i];
		switch( value->type ){
		case DAO_INTERFACE : DaoByteEncoder_EncodeInterface( self, (DaoInterface*) value ); break;
		case DAO_ROUTINE : DaoByteEncoder_EncodeRoutine( self, (DaoRoutine*) value ); break;
		case DAO_CLASS : DaoByteEncoder_EncodeClass( self, (DaoClass*) value ); break;
		}
	}
}
void DaoByteEncoder_Encode( DaoByteEncoder *self, DaoNamespace *nspace, DString *output )
{
	self->nspace = nspace;

	DaoByteEncoder_EncodeNamespace( self, nspace );

	/* Header: */
	DString_Append( output, self->header );

	/* Source: */
	DString_AppendUInt16( output, nspace->name->size );
	DString_AppendString( output, nspace->name );


#ifdef DEBUG_BC
	DString_AppendMBS( output, "Identifier:\n" );
	DString_AppendUInt16( output, self->mapIdentifiers->size );
	DString_Append( output, self->identifiers );
	DString_AppendMBS( output, "Modules:\n" );
	DString_AppendUInt16( output, nspace->loadings->size/2 );
	DString_Append( output, self->modules );
	DString_AppendMBS( output, "Declarations:\n" );
	DString_AppendUInt16( output, self->mapDeclarations->size );
	DString_Append( output, self->declarations );
	DString_AppendMBS( output, "Types:\n" );
	DString_AppendUInt16( output, self->mapTypes->size );
	DString_Append( output, self->types );
	DString_AppendMBS( output, "Values:\n" );
	DString_AppendUInt32( output, self->valueCount );
	DString_Append( output, self->values );
	DString_AppendMBS( output, "Global Constants:\n" );
	DString_AppendUInt16( output, self->constCount );
	DString_Append( output, self->constants );
	DString_AppendMBS( output, "Global Variables:\n" );
	DString_AppendUInt16( output, self->varCount );
	DString_Append( output, self->variables );
	DString_AppendMBS( output, "Global Types:\n" );
	DString_AppendUInt16( output, nspace->abstypes->size );
	DString_Append( output, self->glbtypes );
	DString_AppendMBS( output, "Interfaces:\n" );
	DString_AppendUInt16( output, self->mapInterfaces->size );
	DString_Append( output, self->interfaces );
	DString_AppendMBS( output, "Classes:\n" );
	DString_AppendUInt16( output, self->mapClasses->size );
	DString_Append( output, self->classes );
	DString_AppendMBS( output, "Routines:\n" );
	DString_AppendUInt16( output, self->mapRoutines->size );
	DString_Append( output, self->routines );
	return;
#endif

	/* Identifiers: */
	DString_AppendUInt16( output, self->mapIdentifiers->size );
	DString_Append( output, self->identifiers );

	/* Modules: */
	DString_AppendUInt16( output, nspace->loadings->size/2 );
	DString_Append( output, self->modules );

	/* Declarations: */
	DString_AppendUInt16( output, self->mapDeclarations->size );
	DString_Append( output, self->declarations );

	/* Types: */
	DString_AppendUInt16( output, self->mapTypes->size );
	DString_Append( output, self->types );

	/* Values: */
	DString_AppendUInt32( output, self->valueCount );
	DString_Append( output, self->values );

	/* Global Constants: */
	DString_AppendUInt16( output, self->constCount );
	DString_Append( output, self->constants );

	/* Global Variables: */
	DString_AppendUInt16( output, self->varCount );
	DString_Append( output, self->variables );

	/* Global Types: */
	DString_AppendUInt16( output, nspace->abstypes->size );
	DString_Append( output, self->glbtypes );

	/* Interfaces: */
	DString_AppendUInt16( output, self->mapInterfaces->size );
	DString_Append( output, self->interfaces );

	/* Classes: */
	DString_AppendUInt16( output, self->mapClasses->size );
	DString_Append( output, self->classes );

	/* Routines: */
	DString_AppendUInt16( output, self->mapRoutines->size );
	DString_Append( output, self->routines );
}





DaoByteDecoder* DaoByteDecoder_New( DaoVmSpace *vmspace )
{
	DaoByteDecoder *self = (DaoByteDecoder*) dao_calloc( 1, sizeof(DaoByteDecoder) );
	self->vmspace = vmspace;

	self->identifiers = DArray_New(D_STRING);
	self->namespaces = DArray_New(D_VALUE);
	self->declarations = DArray_New(D_VALUE);
	self->types = DArray_New(D_VALUE);
	self->values = DArray_New(D_VALUE);
	self->interfaces = DArray_New(D_VALUE);
	self->classes = DArray_New(D_VALUE);
	self->routines = DArray_New(D_VALUE);
	self->array = DArray_New(0);
	self->string = DString_New(1);
	self->map = DMap_New(D_STRING,0);
	self->valueTypes = DHash_New(0,0);
	return self;
}
void DaoByteDecoder_Delete( DaoByteDecoder *self )
{
	DArray_Delete( self->identifiers );
	DArray_Delete( self->namespaces );
	DArray_Delete( self->declarations );
	DArray_Delete( self->types );
	DArray_Delete( self->values );
	DArray_Delete( self->interfaces );
	DArray_Delete( self->classes );
	DArray_Delete( self->routines );
	DArray_Delete( self->array );
	DString_Delete( self->string );
	DMap_Delete( self->valueTypes );
	DMap_Delete( self->map );
	dao_free( self );
}

int DaoByteDecoder_DecodeUInt8( DaoByteDecoder *self )
{
	int value;
	if( self->codes >= self->end ) return 0;
	value = self->codes[0];
	self->codes += 1;
	return value;
}
int DaoByteDecoder_DecodeUInt16( DaoByteDecoder *self )
{
	int value;
	if( self->codes >= self->end ) return 0;
	value = (self->codes[0]<<8) + self->codes[1];
	self->codes += 2;
	return value;
}
uint_t DaoByteDecoder_DecodeUInt32( DaoByteDecoder *self )
{
	uint_t value;
	if( self->codes >= self->end ) return 0;
	value = self->codes[0] << 24;
	value += self->codes[1] << 16;
	value += self->codes[2] << 8;
	value += self->codes[3];
	self->codes += 4;
	return value;
}
daoint DaoByteDecoder_DecodeDaoInt( DaoByteDecoder *self )
{
	daoint value = 0;
	uchar_t i, m = sizeof(daoint);
	if( self->codes >= self->end ) return 0;
	for(i=0; i<m; ++i) value += self->codes[i] << 8*(m-1-i);
	self->codes += m;
	return value;
}
/*
// IEEE 754 double-precision binary floating-point format:
//   sign(1)--exponent(11)------------fraction(52)---------------------
//   S EEEEEEEEEEE FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF  
//   63         52                                                    0
//
//   value = (-1)^S  *  ( 1 + \sigma_0^51 (b_i * 2^{-(52-i)}) )  *  2^{E-1023}
*/
double DaoByteDecoder_DecodeDouble( DaoByteDecoder *self )
{
	double value = 1.0;
	uint_t first = DaoByteDecoder_DecodeUInt32( self );
	uint_t second = DaoByteDecoder_DecodeUInt32( self );
	uint_t negative = first & (1<<31);
	int i, expon;
	
	if( self->codes > self->end ) return 0;
	if( first == 0 && second == 0 ) return 0;
	if( first == (0x7FF<<20) && second == 0 ) return INFINITY;
	if( first == (0x7FF<<20) && second == 1 ) return NAN;

	first = (first<<1)>>1;
	expon = (first>>20) - 1023;
	for(i=0; i<32; ++i){
		if( (second>>i)&0x1 ){
			int e = -(52-i);
			if( e >= 0 )
				value += pow( 2, e );
			else
				value += 1.0 / pow( 2, -e );
		}
	}
	for(i=0; i<20; ++i){
		if( (first>>i)&0x1 ){
			int e = -(20-i);
			if( e >= 0 )
				value += pow( 2, e );
			else
				value += 1.0 / pow( 2, -e );
		}
	}
	if( expon >= 0 )
		value *= pow( 2, expon );
	else
		value /= pow( 2, -expon );
	if( negative ) value = -value;
	return value;
}
complex16 DaoByteDecoder_DecodeComplex( DaoByteDecoder *self )
{
	complex16 value = {0.0,0.0};
	value.real = DaoByteDecoder_DecodeDouble( self );
	value.imag = DaoByteDecoder_DecodeDouble( self );
	return value;
}
void DaoByteDecoder_DecodeShortString( DaoByteDecoder *self, DString *string )
{
	int len = DaoByteDecoder_DecodeUInt16( self );
	if( self->codes >= self->end ) return;
	DString_Reset( string, 0 );
	DString_AppendDataMBS( string, (char*) self->codes, len );
	self->codes += len;
}

DaoNamespace* DaoNamespace_LoadModule( DaoNamespace *self, DString *name );

void DaoByteDecoder_DecodeModules( DaoByteDecoder *self )
{
	int i, count = DaoByteDecoder_DecodeUInt16( self );
	printf( "DaoByteDecoder_DecodeModules: %3i\n", count );
	for(i=0; i<count; ++i){
		int id1 = DaoByteDecoder_DecodeUInt16( self );
		int id2 = DaoByteDecoder_DecodeUInt16( self );
		DString *s1 = self->identifiers->items.pString[id1-1];
		DString *s2 = id2 ? self->identifiers->items.pString[id2-1] : NULL;
		printf( "loading: %s %p\n", s1->mbs, s2 );
		DaoNamespace *ns = DaoNamespace_LoadModule( self->nspace, s1 );
		DArray_Append( self->namespaces, ns );
		if( s2 == NULL ) DaoNamespace_AddParent( self->nspace, ns );
	}
}
void DaoByteDecoder_DecodeIdentifiers( DaoByteDecoder *self )
{
	int i, count = DaoByteDecoder_DecodeUInt16( self );
	printf( "DaoByteDecoder_DecodeIdentifiers: %i\n", count );
	for(i=0; i<count; ++i){
		DaoByteDecoder_DecodeShortString( self, self->string );
		DArray_Append( self->identifiers, self->string );
		printf( "%5i: %s\n", i, self->string->mbs );
	}
}
void DaoByteDecoder_DecodeDeclarations( DaoByteDecoder *self )
{
	int i, id, count = DaoByteDecoder_DecodeUInt16( self );
	printf( "DaoByteDecoder_DecodeDeclarations: %i\n", count );
	for(i=0; i<count; ++i){
		int type = DaoByteDecoder_DecodeUInt8( self );
		int subtype = DaoByteDecoder_DecodeUInt8( self );
		int dectype = DaoByteDecoder_DecodeUInt8( self );
		int nameid = DaoByteDecoder_DecodeUInt16( self );
		int hostid = DaoByteDecoder_DecodeUInt16( self );
		int fieldid = DaoByteDecoder_DecodeUInt16( self );
		DString *name = NULL, *field = NULL;
		DaoType *hostype = NULL;
		DaoValue *host = (DaoValue*) self->nspace;
		DaoValue *value = NULL;
		DaoInterface *inter;
		DaoRoutine *routine;
		DaoClass *klass;
		DNode *it;

		if( fieldid ) field = self->identifiers->items.pString[fieldid-1];
		if( hostid ){
			host = self->declarations->items.pValue[hostid-1];
			switch( host->type ){
			case DAO_CLASS : hostype = host->xClass.objType; break;
			case DAO_INTERFACE : hostype = host->xInterface.abtype; break;
			}
		}
		if( dectype == DAO_DECL_CREATE ){
			int body = hostype == NULL || hostype->tid != DAO_INTERFACE;
			if( nameid ) name = self->identifiers->items.pString[nameid-1];
			switch( type ){
			case DAO_ROUTINE :
				if( subtype == DAO_ROUTINE_OVERLOADED ){
					routine = DaoRoutines_New( self->nspace, hostype, NULL );
				}else{
					routine = DaoRoutine_New( self->nspace, hostype, body );
				}
				DString_Assign( routine->routName, name );
				value = (DaoValue*) routine;
				break;
			case DAO_CLASS :
				klass = DaoClass_New();
				value = (DaoValue*) klass;
				DaoClass_SetName( klass, name, self->nspace );
				break;
			case DAO_INTERFACE :
				inter = DaoInterface_New( self->nspace, name->mbs );
				value = (DaoValue*) inter;
				break;
			default :
				break;
			}
		}else if( dectype == DAO_DECL_LOADAS ){
			value = self->namespaces->items.pValue[nameid-1];
		}else{
			if( nameid ) name = self->identifiers->items.pString[nameid-1];
			if( field == NULL ) field = name;
			printf( "%i %i\n", host->type, DAO_NAMESPACE );
			switch( host->type ){
			case DAO_NAMESPACE :
				value = DaoNamespace_GetData( (DaoNamespace*) host, field );
				printf( "%p %s\n", value, field->mbs );
				break;
			case DAO_CLASS :
				id = DaoClass_FindConst( (DaoClass*)host, field );
				printf( "%i\n", id );
				if( id >= 0 ) value = DaoClass_GetConst( (DaoClass*)host, id );
				break;
			case DAO_CTYPE :
				value = DaoType_FindValue( host->xCtype.ctype, field );
				break;
			case DAO_INTERFACE :
				it = DMap_Find( host->xInterface.methods, field );
				if( it ) value = it->value.pValue;
				break;
			default :
				break;
			}
		}
		printf( ">>>>>> decl %3i: %3i %3i %p %s\n", i, type, nameid, value, name?name->mbs:"" );
		DArray_Append( self->declarations, value );
	}
}
void DaoByteDecoder_DecodeTypes( DaoByteDecoder *self )
{
	DNode *it;
	DaoValue *value;
	DaoType **types;
	int i, j, flag, count, numtypes = DaoByteDecoder_DecodeUInt16( self );
	int id, id2, id3;
	const char *tname;

	printf( "DaoByteDecoder_DecodeTypes: %3i\n", numtypes );
	for(i=0; i<numtypes; ++i){
		DaoValue *aux = NULL;
		DaoType *type2, *type = NULL;
		int tid = DaoByteDecoder_DecodeUInt8( self );
		int attrib = DaoByteDecoder_DecodeUInt8( self );
		int nameid = DaoByteDecoder_DecodeUInt16( self );
		DString *name = nameid ? self->identifiers->items.pString[nameid-1] : NULL;
		DString *name2;

		//printf( "%3i : %i %i %p\n", i, tid, nameid, name );
		//printf( "%3i : %i %i %s\n", i, tid, nameid, name?name->mbs:"NULL" );
		switch( tid ){
		case DAO_NONE :
			type = dao_type_none;
			break;
		case DAO_INTEGER :
			type = dao_type_int;
			break;
		case DAO_FLOAT :
			type = dao_type_float;
			break;
		case DAO_DOUBLE :
			type = dao_type_double;
			break;
		case DAO_COMPLEX :
			type = dao_type_complex;
			break;
		case DAO_LONG :
			type = dao_type_long;
			break;
		case DAO_STRING :
			type = dao_type_string;
			break;
		case DAO_ENUM :
			flag = DaoByteDecoder_DecodeUInt8( self );
			count = DaoByteDecoder_DecodeUInt16( self );
			DString_SetMBS( self->string, "enum<" );
			DMap_Reset( self->map );
			for(j=0; j<count; ++j){
				int symID = DaoByteDecoder_DecodeUInt16( self );
				int value = DaoByteDecoder_DecodeUInt32( self );
				DString *sym = self->identifiers->items.pString[symID-1];
				if( j ) DString_AppendChar( self->string, flag ? ';' : ',' );
				DString_Append( self->string, sym );
				DMap_Insert( self->map, sym, IntToPointer( value ) );
				// XXX: append =value?
			}
			DString_AppendChar( self->string, '>' );
			it = DMap_First( self->map );
			if( count == 1 && it->value.pInt == 0 ){
				DString_SetMBS( self->string, "$" );
				DString_Append( self->string, it->key.pString );
			}
			type = DaoNamespace_FindType( self->nspace, self->string );
			if( type == NULL ){
				type = DaoType_New( self->string->mbs, DAO_ENUM, NULL, NULL );
				type->mapNames = self->map;
				self->map = DMap_New(D_STRING,0);
				type->flagtype = flag != 0;
			}
			break;
		case DAO_ARRAY : case DAO_LIST : case DAO_MAP :
		case DAO_TUPLE : case DAO_TYPE : case DAO_FUTURE :
		case DAO_VARIANT :
			if( tid == DAO_VARIANT ){
				id = DaoByteDecoder_DecodeUInt16( self );
				if( id ) aux = self->types->items.pValue[id-1];
			}
			self->array->size = 0;
			count = DaoByteDecoder_DecodeUInt16( self );
			for(j=0; j<count; ++j){
				id = DaoByteDecoder_DecodeUInt16( self );
				DArray_Append( self->array, self->types->items.pType[id-1] );
			}
			types = self->array->items.pType;
			switch( tid ){
			case DAO_ARRAY   : tname = "array"; break;
			case DAO_LIST    : tname = "list"; break;
			case DAO_MAP     : tname = "map"; break;
			case DAO_TUPLE   : tname = "tuple"; break;
			case DAO_TYPE    : tname = "type"; break;
			case DAO_FUTURE  : tname = "future"; break;
			case DAO_VARIANT : tname = aux ? aux->xType.name->mbs : ""; break;
			}
			type = DaoNamespace_MakeType( self->nspace, tname, tid, aux, types, count );
			break;
		case DAO_VALTYPE :
			id = DaoByteDecoder_DecodeUInt32( self );
			type = DaoType_New( name->mbs, DAO_VALTYPE, NULL, NULL );
			DMap_Insert( self->valueTypes, IntToPointer( id - 1 ), type );
			break;
		case DAO_PAR_NAMED :
		case DAO_PAR_DEFAULT :
			id = DaoByteDecoder_DecodeUInt16( self );
			id2 = DaoByteDecoder_DecodeUInt16( self );
			name2 = self->identifiers->items.pString[id-1];
			value = self->types->items.pValue[id2-1];
			type = DaoNamespace_MakeType( self->nspace, name2->mbs, tid, value, NULL, 0 );
			break;
		case DAO_PAR_VALIST :
			type = DaoNamespace_MakeType( self->nspace, "...", DAO_PAR_VALIST, 0,0,0 );
			break;
		case DAO_CODEBLOCK :
			id = DaoByteDecoder_DecodeUInt16( self );
			count = DaoByteDecoder_DecodeUInt16( self );
			self->array->size = 0;
			for(j=0; j<count; ++j){
				int it = DaoByteDecoder_DecodeUInt16( self );
				DArray_Append( self->array, self->types->items.pType[it-1] );
			}
			value = id ? self->types->items.pValue[id-1] : NULL;
			types = self->array->items.pType;
			type = DaoNamespace_MakeType( self->nspace, "", tid|(attrib<<16), value, types, count );
			break;
		case DAO_ROUTINE :
			id = DaoByteDecoder_DecodeUInt16( self );
			id2 = DaoByteDecoder_DecodeUInt16( self );
			id3 = DaoByteDecoder_DecodeUInt16( self );
			count = DaoByteDecoder_DecodeUInt16( self );
			self->array->size = 0;
			for(j=0; j<count; ++j){
				int it = DaoByteDecoder_DecodeUInt16( self );
				DArray_Append( self->array, self->types->items.pType[it-1] );
			}
			types = self->array->items.pType;
			if( id ){
				value = self->declarations->items.pValue[id-1];
				// XXX error:
				//if( value == NULL || value->type != DAO_ROUTINE ) return;
				printf( "value = %p\n", value );
				type = value->xRoutine.routType;
			}else{
				value = id2 ? self->types->items.pValue[id2-1] : NULL;
				//if( value == NULL || value->type != DAO_TYPE ) return;
				type = DaoNamespace_MakeType( self->nspace, "routine", tid|(attrib<<16), value, types, count );
				if( id3 ){
					DString *name = self->string;
					DaoType *cbtype = self->types->items.pType[id3-1];
					DaoType *type2;
					DString_Assign( name, type->name );
					DString_Append( name, cbtype->name ); 
					type = DaoType_New( name->mbs, DAO_ROUTINE, type->aux, type->nested );
					printf( "aaaaaaaaaaaaa %p %p %p\n", type, type->aux, value );
					type->attrib = attrib;
					type->cbtype = cbtype;
					GC_IncRC( cbtype );
					type2 = DaoNamespace_FindType( self->nspace, name );
					if( DaoType_MatchTo( type, type2, NULL ) == DAO_MT_EQ ){
						GC_IncRC( type );
						GC_DecRC( type );
						type = type2;
					printf( "aaaaaaaaaaaaa b %p %p\n", type->aux, value );
					}else{
						DaoNamespace_AddType( self->nspace, name, type );
					}
				}
			}
			break;
		case DAO_INTERFACE :
			id = DaoByteDecoder_DecodeUInt16( self );
			type = self->declarations->items.pInter[id-1]->abtype;
			break;
		case DAO_CLASS :
			id = DaoByteDecoder_DecodeUInt16( self );
			type = self->declarations->items.pClass[id-1]->clsType;
			break;
		case DAO_OBJECT :
			id = DaoByteDecoder_DecodeUInt16( self );
			type = self->declarations->items.pClass[id-1]->objType;
			break;
		case DAO_CTYPE :
			id = DaoByteDecoder_DecodeUInt16( self );
			type = self->declarations->items.pCtype[id-1]->ctype;
			break;
		case DAO_CDATA :
			id = DaoByteDecoder_DecodeUInt16( self );
			type = self->declarations->items.pCtype[id-1]->cdtype;
			break;
		case DAO_ANY :
			type = DaoNamespace_MakeType( self->nspace, "any", tid, NULL, NULL, 0 );
			break;
		case DAO_THT : 
			type = DaoNamespace_MakeType( self->nspace, name->mbs, tid, NULL, NULL, 0 );
			break;
		case DAO_NAMESPACE :
			type = DaoNamespace_MakeType( self->nspace, "namespace", tid, NULL, NULL, 0 );
			break;
		default :
			type = DaoNamespace_MakeType( self->nspace, name->mbs, tid, NULL, NULL, 0 );
			break;
		}
		printf( "-------------- %3i %3i %15p %s\n", i, tid, type, name? name->mbs:"NULL" );
		if( type == NULL ) exit(0);
		if( attrib & DAO_TYPE_COROUTINE ){
			printf( "#################### %s %p\n", type->name->mbs, type );
		}
		type->attrib = attrib;
		DaoType_CheckAttributes( type );
		DArray_Append( self->types, type );
		// XXX if name != type->name, alias;
	}
	for(i=0; i<self->types->size; ++i){
		DaoType *type = self->types->items.pType[i];
		if( type->attrib & DAO_TYPE_COROUTINE ){
			printf( "#################### %s %p\n", type->name->mbs, type );
		}
	}
}
void DaoArray_ResizeData( DaoArray *self, daoint size, daoint old );
void DaoByteDecoder_DecodeValues( DaoByteDecoder *self )
{
	DaoCtype *ctype;
	DaoCdata *cdata;
	DaoClass *klass;
	DaoObject *object;
	DaoRoutine *routine;
	DaoInterface *inter;
	DaoNameValue *nameva;
	DaoString *stringValue;
	DaoLong *longValue;
	DaoArray *array;
	DaoList *list;
	DaoMap *map;
	DaoTuple *tuple;
	DaoType *type;
	DaoType *hostype;
	double fvalue;
	complex16 cvalue;
	daoint k, ivalue, ivalue2;
	int numvalues = DaoByteDecoder_DecodeUInt32( self );
	int i, j, flag, count;
	int id, id2, id3;
	uint_t hashing;

	printf( "DaoByteDecoder_DecodeValues: %3i\n", numvalues );
	for(i=0; i<numvalues; ++i){
		int tid = DaoByteDecoder_DecodeUInt8( self );
		DaoValue *value = NULL;
		DString *name;
		DNode *it;

		printf( ">>>>>>>>>>>>>>>>> %3i: %3i\n", i, tid );
		switch( tid ){
		case DAO_NONE :
			value = dao_none_value;
			break;
		case DAO_INTEGER :
			ivalue = DaoByteDecoder_DecodeDaoInt( self );
			value = (DaoValue*) DaoInteger_New( ivalue );
			break;
		case DAO_FLOAT :
			fvalue = DaoByteDecoder_DecodeDouble( self );
			value = (DaoValue*) DaoFloat_New( fvalue );
			break;
		case DAO_DOUBLE :
			fvalue = DaoByteDecoder_DecodeDouble( self );
			value = (DaoValue*) DaoDouble_New( fvalue );
			break;
		case DAO_COMPLEX :
			cvalue = DaoByteDecoder_DecodeComplex( self );
			value = (DaoValue*) DaoComplex_New( cvalue );
			break;
		case DAO_LONG :
			flag = DaoByteDecoder_DecodeUInt8( self );
			ivalue = DaoByteDecoder_DecodeDaoInt( self );
			longValue = DaoLong_New();
			longValue->value->sign = flag ? -1 : 1;
			value = (DaoValue*) longValue;
			for(j =0; j<ivalue; ++j){
				uchar_t d = DaoByteDecoder_DecodeUInt8( self );
				DLong_PushBack( longValue->value, d );
			}
			break;
		case DAO_STRING :
			flag = DaoByteDecoder_DecodeUInt8( self );
			ivalue = DaoByteDecoder_DecodeDaoInt( self );
			stringValue = DaoString_New( (flag>>4) != 0 );
			value = (DaoValue*) stringValue;
			if( flag >> 4 ){
				DString_AppendDataMBS( stringValue->data, (char*)self->codes, ivalue );
				self->codes += ivalue;
			}else{
				flag = flag & 0xf;
				DString_Resize( stringValue->data, ivalue );
				for(j =0; j<ivalue; ++j){
					wchar_t ch = 0;
					switch( flag ){
					case 1 : ch = DaoByteDecoder_DecodeUInt8( self ); break;
					case 2 : ch = DaoByteDecoder_DecodeUInt16( self ); break;
					case 4 : ch = DaoByteDecoder_DecodeUInt32( self ); break;
					}
					stringValue->data->wcs[j] = ch;
				}
			}
			break;
		case DAO_ENUM :
			id = DaoByteDecoder_DecodeUInt16( self );
			id2 = DaoByteDecoder_DecodeUInt32( self );
			printf( "%i\n", id );
			type = id ? self->types->items.pType[id-1] : NULL;
			value = (DaoValue*) DaoEnum_New( type, id2 );
			break;
		case DAO_ARRAY :
			flag = DaoByteDecoder_DecodeUInt8( self );
			ivalue = DaoByteDecoder_DecodeDaoInt( self );
			count = DaoByteDecoder_DecodeUInt16( self );
			array = DaoArray_New( flag );
			value = (DaoValue*) array;
			DaoArray_SetDimCount( array, count );
			for(j=0; j<count; ++j) array->dims[j] = DaoByteDecoder_DecodeDaoInt( self );
			DaoArray_FinalizeDimData( array );
			DaoArray_ResizeData( array, array->size, 0 );
			assert( array->size == ivalue );
			for(k=0; k<ivalue; ++k){
				switch( flag ){
				case DAO_INTEGER: array->data.i[k] = DaoByteDecoder_DecodeDaoInt(self); break;
				case DAO_FLOAT  : array->data.f[k] = DaoByteDecoder_DecodeDouble(self); break;
				case DAO_DOUBLE : array->data.d[k] = DaoByteDecoder_DecodeDouble(self); break;
				case DAO_COMPLEX: array->data.c[k] = DaoByteDecoder_DecodeComplex(self);break;
				}
			}
			break;
		case DAO_LIST :
			id = DaoByteDecoder_DecodeUInt16( self );
			ivalue = DaoByteDecoder_DecodeDaoInt( self );
			type = id ? self->types->items.pType[id-1] : NULL;
			list = DaoList_New();
			value = (DaoValue*) list;
			GC_ShiftRC( type, list->unitype );
			list->unitype = type;
			printf( "type = %i %i %p %s\n", id, ivalue, type, type->name->mbs );
			for(k=0; k<ivalue; ++k){
				uint_t it = DaoByteDecoder_DecodeUInt32( self );
				printf( "it = %i\n", it );
				DaoList_PushBack( list, self->values->items.pValue[it-1] );
			}
			break;
		case DAO_MAP :
			hashing = DaoByteDecoder_DecodeUInt32( self );
			id = DaoByteDecoder_DecodeUInt16( self );
			ivalue = DaoByteDecoder_DecodeDaoInt( self );
			type = id ? self->types->items.pType[id-1] : NULL;
			map = DaoMap_New( hashing );
			GC_ShiftRC( type, map->unitype );
			map->unitype = type;
			value = (DaoValue*) map;
			for(k=0; k<ivalue; ++k){
				uint_t key = DaoByteDecoder_DecodeUInt32( self );
				uint_t val = DaoByteDecoder_DecodeUInt32( self );
				DaoValue *key2 = self->values->items.pValue[key-1];
				DaoValue *val2 = self->values->items.pValue[val-1];
				DaoMap_Insert( map, key2, val2 );
			}
			break;
		case DAO_TUPLE :
			id = DaoByteDecoder_DecodeUInt16( self );
			ivalue = DaoByteDecoder_DecodeUInt32( self );
			type = id ? self->types->items.pType[id-1] : NULL;
			tuple = type ? DaoTuple_Create( type, ivalue, 0 ) : DaoTuple_New( ivalue );
			value = (DaoValue*) tuple;
			for(k=0; k<ivalue; ++k){
				uint_t it = DaoByteDecoder_DecodeUInt32( self );
				DaoValue *val = self->values->items.pValue[it-1];
				DaoTuple_SetItem( tuple, val, k );
				printf( "%i %p %i %s\n", k, tuple->items[k], val->xInteger.value, type->name->mbs );
				assert( tuple->items[k] != NULL );
			}
			break;
		case DAO_ROUTINE :
			id = DaoByteDecoder_DecodeUInt16( self );
			id2 = DaoByteDecoder_DecodeUInt16( self );
			id3 = DaoByteDecoder_DecodeUInt16( self );
			flag = DaoByteDecoder_DecodeUInt8( self );
			routine = self->declarations->items.pRoutine[id-1];
			type = self->types->items.pType[id2-1];
			hostype = id3 ? self->types->items.pType[id3-1] : NULL;
			routine->parCount = type->variadic ? DAO_MAX_PARAM : type->nested->size;
			routine->attribs = flag;
			printf( "%i %i  %p %p\n", id, id2, routine, type );
			GC_ShiftRC( type, routine->routType );
			routine->routType = type;
			GC_ShiftRC( hostype, routine->routHost );
			routine->routHost = hostype;
			value = (DaoValue*) routine;
			printf( "########## %p %p\n", routine, routine->routType );
			break;
		case DAO_OBJECT :
			flag = DaoByteDecoder_DecodeUInt8( self );
			id = DaoByteDecoder_DecodeUInt16( self );
			klass = self->declarations->items.pClass[id-1];
			if( flag == 0x2 ){
				value = klass->objType->value;
			}else if( flag == 0x1 ){
				//XXX
			}else{
			}
			break;
		case DAO_INTERFACE :
			id = DaoByteDecoder_DecodeUInt16( self );
			value = self->declarations->items.pValue[id-1];
			break;
		case DAO_CLASS :
		case DAO_CTYPE :
			id = DaoByteDecoder_DecodeUInt16( self );
			value = self->declarations->items.pValue[id-1];
			break;
		case DAO_CDATA :
			id = DaoByteDecoder_DecodeUInt16( self );
			break;
		case DAO_TYPE :
			id = DaoByteDecoder_DecodeUInt16( self );
			value = id ? self->types->items.pValue[id-1] : NULL;
			break;
		case DAO_FUTURE :
			id = DaoByteDecoder_DecodeUInt16( self );
			break;
		case DAO_NAMESPACE : // XXX
			id = DaoByteDecoder_DecodeUInt16( self );
			value = id ? self->declarations->items.pValue[id-1] : (DaoValue*)self->nspace;
			break;
		case DAO_PAR_NAMED :
			id = DaoByteDecoder_DecodeUInt16( self );
			id2 = DaoByteDecoder_DecodeUInt16( self );
			id3 = DaoByteDecoder_DecodeUInt32( self );
			printf( "id3 = %i; %i %i\n", id3, id2, id );
			name = self->identifiers->items.pString[id-1];
			value = id3 ? self->values->items.pValue[id3-1] : NULL;
			printf( "!!!!!!!!!!!!!!!!!!!!!!!!!!! %f\n", value->xDouble.value );
			nameva = DaoNameValue_New( name, value );
			value = (DaoValue*) nameva;
			if( id2 ){
				nameva->unitype = self->types->items.pType[id2-1];
				GC_IncRC( nameva->unitype );
			}
			break;
		default:
			printf( "value type not supported %i\n", tid );
			assert(0);
			break;
		}
		DArray_Append( self->values, NULL );
		GC_IncRC( value );
		self->values->items.pValue[ self->values->size - 1 ] = value;
		it = DMap_Find( self->valueTypes, IntToPointer(self->values->size-1) );
		if( it ){
			DaoType *valtype = it->value.pType;
			DaoValue_Move( value, & valtype->aux, NULL );
		}
	}
}
void DaoByteDecoder_DecodeConstants( DaoByteDecoder *self )
{
	DArray *constants = self->nspace->constants;
	int i, j, flag, count, numconsts = DaoByteDecoder_DecodeUInt16( self );
	int id, id2, id3;
	printf( "DaoByteDecoder_DecodeConstants: %3i\n", numconsts );
	for(i=0; i<numconsts; ++i){
		int nameID = DaoByteDecoder_DecodeUInt16( self );
		int index = DaoByteDecoder_DecodeUInt16( self );
		int perm = DaoByteDecoder_DecodeUInt8( self );
		int valueID = DaoByteDecoder_DecodeUInt32( self );
		printf( "%3i:  :::::    %i\n", i, valueID );
		DString *name = nameID ? self->identifiers->items.pString[nameID-1] : NULL;
		DaoValue *value = valueID ? self->values->items.pValue[valueID-1] : NULL;

		if( name ) printf( "%3i : %s\n", i, name->mbs );
		if( index > constants->size ) goto DecodingError;
		printf( "%i %p\n", valueID, value );
		assert( value != NULL );

		if( index == LOOKUP_ID(DVR_NSC_MAIN) && value && value->type == DAO_ROUTINE ){
			if( index >= constants->size ) goto DecodingError;
			GC_ShiftRC( value, constants->items.pConst[index]->value );
			GC_ShiftRC( value, self->nspace->mainRoutine );
			self->nspace->mainRoutine = (DaoRoutine*) value;
			constants->items.pConst[index]->value = value;
		}else if( name && value && (value->type == DAO_ROUTINE || index == constants->size) ){
			int id = DaoNamespace_AddConst( self->nspace, name, value, perm );
			printf( "@@@@@@@@@@@@@@@ %i %i %s\n", id, value->type, name->mbs );
			if( id < 0 ) goto DecodingError;
		}else if( index == constants->size ){
			DArray_Append( constants, DaoConstant_New( value ) );
		}else{
			printf( "@@@@@@@@@@@@@@@@@@ %i %i\n", index, value->type );
			DaoNamespace_SetConst( self->nspace, index, value );
		}
		if( name ){
			int lk = DaoNamespace_FindConst( self->nspace, name );
			printf( ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %i %s\n", lk, name->mbs );
			printf( "%i %i\n", index, LOOKUP_ID( lk ) );
			if( lk >= 0 && index != LOOKUP_ID( lk ) ) goto DecodingError;
		}
	}
	return;
DecodingError:
	self->codes = self->error;
}
void DaoByteDecoder_DecodeVariables( DaoByteDecoder *self )
{
	DArray *variables = self->nspace->variables;
	int i, j, flag, count, numvars = DaoByteDecoder_DecodeUInt16( self );
	int id, id2, id3;
	printf( "DaoByteDecoder_DecodeVariables: %3i\n", numvars );
	for(i=0; i<numvars; ++i){
		int nameID = DaoByteDecoder_DecodeUInt16( self );
		int index = DaoByteDecoder_DecodeUInt16( self );
		int perm = DaoByteDecoder_DecodeUInt8( self );
		int typeID = DaoByteDecoder_DecodeUInt16( self );
		int valueID = DaoByteDecoder_DecodeUInt32( self );
		DString *name = nameID ? self->identifiers->items.pString[nameID-1] : NULL;
		DaoType *type = typeID ? self->types->items.pType[typeID-1] : NULL;
		DaoValue *value = valueID ? self->values->items.pValue[valueID-1] : NULL;

		if( name ) printf( "%3i : %i %i %s\n", i, index, variables->size, name->mbs );

		if( index > variables->size ) goto DecodingError;

		if( name && value ){
			int id = DaoNamespace_AddVariable( self->nspace, name, value, type, perm );
			printf( "@@@@@@@@@@@@@@@ %i %i %i %s\n", id, value->type, type->tid, name->mbs );
			if( id < 0 ) goto DecodingError;
		}else if( index == variables->size ){
			DArray_Append( variables, DaoVariable_New( value, type ) );
		}else{
			printf( "@@@@@@@@@@@@@@@@@@ %i %i\n", index, value->type );
			DaoVariable_Set( variables->items.pVar[index], value, type );
		}
		if( name ){
			int lk = DaoNamespace_FindVariable( self->nspace, name );
			if( lk >= 0 && index != LOOKUP_ID( lk ) ) goto DecodingError;
		}
	}
	return;
DecodingError:
	self->codes = self->error;
}
void DaoByteDecoder_DecodeGlobalTypes( DaoByteDecoder *self )
{
	int i, j, flag, count, numtypes = DaoByteDecoder_DecodeUInt16( self );
	int id, id2, id3;
	printf( "DaoByteDecoder_DecodeGlobalTypes: %3i\n", numtypes );
	for(i=0; i<numtypes; ++i){
		int nameID = DaoByteDecoder_DecodeUInt16( self );
		int typeID = DaoByteDecoder_DecodeUInt16( self );
		DString *name = nameID ? self->identifiers->items.pString[nameID-1] : NULL;
		DaoType *type = typeID ? self->types->items.pType[typeID-1] : NULL;
		if( name ) printf( ">>>>>>>>>>>>>>>>>>>>>>>>>> %3i : %s %i %p\n", i, name->mbs, typeID, type );
		assert( type != NULL );
		if( type->attrib & DAO_TYPE_COROUTINE ){
			printf( "#################### %s %p\n", type->name->mbs, type );
		}
		DaoNamespace_AddType( self->nspace, name, type );
	}
}
void DaoByteDecoder_DecodeInterfaces( DaoByteDecoder *self )
{
	int i, j, flag, count, num = DaoByteDecoder_DecodeUInt16( self );
	int id, id2, id3;
	printf( "DaoByteDecoder_DecodeInterfaces: %3i\n", num );
	for(i=0; i<num; ++i){
		int interID = DaoByteDecoder_DecodeUInt16( self );
		DaoInterface *inter = self->declarations->items.pInter[interID-1];
		count = DaoByteDecoder_DecodeUInt16( self );
		for(j=0; j<count; ++j){
			int supid = DaoByteDecoder_DecodeUInt16( self );
			DaoValue *sup = self->declarations->items.pValue[supid-1];
			DArray_Append( inter->supers, sup );
		}
		DaoInterface_DeriveMethods( inter );
		count = DaoByteDecoder_DecodeUInt16( self );
		for(j=0; j<count; ++j){
			int methid = DaoByteDecoder_DecodeUInt32( self );
			DaoRoutine *rout = self->values->items.pRoutine[methid-1];
			DaoMethods_Insert( inter->methods, rout, self->nspace, rout->routHost );
		}
	}
}
void DaoByteDecoder_DecodeClasses( DaoByteDecoder *self )
{
	int i, j, flag, count, num = DaoByteDecoder_DecodeUInt16( self );
	int id, id2, id3;
	printf( "DaoByteDecoder_DecodeClasses: %3i\n", num );
	for(i=0; i<num; ++i){
		int classID = DaoByteDecoder_DecodeUInt16( self );
		int cstrID = DaoByteDecoder_DecodeUInt16( self );
		int attribs = DaoByteDecoder_DecodeUInt16( self );
		DaoClass *klass = self->declarations->items.pClass[classID-1];
		DaoRoutine *rout = self->declarations->items.pRoutine[cstrID-1];
		DArray *constants = klass->constants;
		DArray *variables = klass->variables;
		DArray *instvars = klass->instvars;
		printf( ">>>>>>>> %3i : %s\n", i, klass->className->mbs );
		GC_ShiftRC( rout, klass->classRoutine );
		klass->classRoutine = rout;
		count = DaoByteDecoder_DecodeUInt16( self );
		for(j=0; j<count; ++j){
			int supid = DaoByteDecoder_DecodeUInt16( self );
			DaoValue *sup = self->declarations->items.pValue[supid-1];
			DaoClass_AddSuperClass( klass, sup );
		}
		DaoClass_DeriveClassData( klass );
		count = DaoByteDecoder_DecodeUInt16( self );
		printf( "constants: %i\n", count );
		for(j=0; j<count; ++j){
			int nameID = DaoByteDecoder_DecodeUInt16( self );
			int index = DaoByteDecoder_DecodeUInt16( self );
			int perm = DaoByteDecoder_DecodeUInt8( self );
			int valueID = DaoByteDecoder_DecodeUInt32( self );
			DString *name = nameID ? self->identifiers->items.pString[nameID-1] : NULL;
			DaoValue *value = valueID ? self->values->items.pValue[valueID-1] : NULL;

			printf( "A: %3i %3i %3i %s\n", j, index, constants->size, name->mbs );
			if( index > constants->size ) goto DecodingError;

			if( name && value && (value->type == DAO_ROUTINE || index == constants->size) ){
				int id = DaoClass_AddConst( klass, name, value, perm );
				printf( "id = %i\n", id );
				if( id < 0 ) goto DecodingError;
			}else if( index == constants->size ){
				int id = DaoClass_AddConst( klass, name, value, perm );
				printf( "id = %i\n", id );
				if( id < 0 ) goto DecodingError;
			}else{
				DaoClass_SetConst( klass, index, value );
			}
			if( name ){
				int lk = DaoClass_FindConst( klass, name );
				if( lk >= 0 && index != LOOKUP_ID( lk ) ) goto DecodingError;
			}
			printf( "B: %3i %3i %3i\n", j, index, constants->size );
		}
		count = DaoByteDecoder_DecodeUInt16( self );
		printf( "variables: %i\n", count );
		for(j=0; j<count; ++j){
			int nameID = DaoByteDecoder_DecodeUInt16( self );
			int index = DaoByteDecoder_DecodeUInt16( self );
			int perm = DaoByteDecoder_DecodeUInt8( self );
			int typeID = DaoByteDecoder_DecodeUInt16( self );
			int valueID = DaoByteDecoder_DecodeUInt32( self );
			DString *name = nameID ? self->identifiers->items.pString[nameID-1] : NULL;
			DaoType *type = typeID ? self->types->items.pType[typeID-1] : NULL;
			DaoValue *value = valueID ? self->values->items.pValue[valueID-1] : NULL;

			printf( "A: %3i %3i %3i\n", j, index, variables->size );
			if( index > variables->size ) goto DecodingError;

			if( index == variables->size ){
				int id = DaoClass_AddGlobalVar( klass, name, value, type, perm );
				if( id < 0 ) goto DecodingError;
			}else{
				DaoVariable_Set( variables->items.pVar[index], value, type );
			}
			if( name ){
				int lk = DaoClass_GetDataIndex( klass, name );
				if( lk >= 0 && index != LOOKUP_ID( lk ) ) goto DecodingError;
			}
			printf( "B: %3i %3i %3i\n", j, index, variables->size );
		}
		count = DaoByteDecoder_DecodeUInt16( self );
		printf( "instvars: %i\n", count );
		for(j=0; j<count; ++j){
			int nameID = DaoByteDecoder_DecodeUInt16( self );
			int index = DaoByteDecoder_DecodeUInt16( self );
			int perm = DaoByteDecoder_DecodeUInt8( self );
			int typeID = DaoByteDecoder_DecodeUInt16( self );
			int valueID = DaoByteDecoder_DecodeUInt32( self );
			DString *name = nameID ? self->identifiers->items.pString[nameID-1] : NULL;
			DaoType *type = typeID ? self->types->items.pType[typeID-1] : NULL;
			DaoValue *value = valueID ? self->values->items.pValue[valueID-1] : NULL;

			printf( "A: %3i %3i %3i %s\n", j, index, instvars->size, name->mbs );
			if( index > instvars->size ) goto DecodingError;

			if( index == instvars->size ){
				int id = DaoClass_AddObjectVar( klass, name, value, type, perm );
				printf( "id = %i %s %s\n", id, name->mbs, type->name->mbs );
				if( id < 0 ) goto DecodingError;
			}else{
				DaoVariable_Set( instvars->items.pVar[index], value, type );
			}
			printf( "B: %3i %3i %3i\n", j, index, instvars->size );
			if( name ){
				int lk = DaoClass_GetDataIndex( klass, name );
				printf( "lk = %i, %i\n", LOOKUP_ID( lk ), index );
				if( lk >= 0 && index != LOOKUP_ID( lk ) ) goto DecodingError;
			}
			printf( "C: %3i %3i %3i\n", j, index, instvars->size );
		}
		count = DaoByteDecoder_DecodeUInt16( self );
		for(j=0; j<count; ++j){
			int name = DaoByteDecoder_DecodeUInt16( self );
			int typeid = DaoByteDecoder_DecodeUInt16( self );
		}
		DaoClass_DeriveObjectData( klass );
		klass->attribs = attribs;
	}
	return;
DecodingError:
	self->codes = self->error;
}
void DaoByteDecoder_DecodeRoutines( DaoByteDecoder *self )
{
	int i, j, flag, count, num = DaoByteDecoder_DecodeUInt16( self );
	int id, id2, id3;
	printf( "DaoByteDecoder_DecodeRoutines: %3i\n", num );
	for(i=0; i<num; ++i){
		int routid = DaoByteDecoder_DecodeUInt16( self );
		int nameid = DaoByteDecoder_DecodeUInt16( self );
		int typeid = DaoByteDecoder_DecodeUInt16( self );
		int hostid = DaoByteDecoder_DecodeUInt16( self );
		int attribs = DaoByteDecoder_DecodeUInt16( self );
		int line = DaoByteDecoder_DecodeUInt16( self );
		int count = DaoByteDecoder_DecodeUInt16( self );
		DaoRoutine *routine = self->declarations->items.pRoutine[routid-1];
		DaoType *routype = typeid ? self->types->items.pType[typeid-1] : NULL;
		DaoType *hostype = hostid ? self->types->items.pType[hostid-1] : NULL;
		DaoInferencer *inferencer = DaoInferencer_New();
		assert( routype == routine->routType );
		assert( hostype == routine->routHost );
		assert( attribs == routine->attribs );
		if( nameid ) 
			printf( "%3i : %s\n", i, self->identifiers->items.pString[nameid-1]->mbs );
		printf( "%i %p %s\n", hostid, hostype, hostype?hostype->name->mbs:"" );
		routine->defLine = line;
		printf( "codes %3i %p\n", count, routine->routType );
		if( (attribs & DAO_ROUT_INITOR) && hostype && hostype->tid == DAO_OBJECT ){
			printf( "here:::::::::::::::::::::  %s\n", routine->routType->name->mbs );
			//DRoutines_Add( hostype->aux->xClass.classRoutines->overloads, routine );
		}
		for(j=0; j<count; ++j){
			int valueid = DaoByteDecoder_DecodeUInt32( self );
			DaoValue *value = valueid ? self->values->items.pValue[valueid-1] : NULL;
			DaoRoutine_AddConstant( routine, value );
		}
		count = DaoByteDecoder_DecodeUInt16( self );
		routine->body->regCount = count;
#if 0
		DArray_Resize( routine->body->regType, count, 0 );
		printf( "codes %3i\n", count );
		for(j=0; j<count; ++j){
			int vatypeid = DaoByteDecoder_DecodeUInt16( self );
			DaoType *type = vatypeid ? self->types->items.pType[vatypeid-1] : NULL;
			GC_ShiftRC( type, routine->body->regType->items.pType[j] );
			routine->body->regType->items.pType[j] = type;
		}
#endif
		count = DaoByteDecoder_DecodeUInt16( self );
		printf( "explicits: %3i\n", count );
		for(j=0; j<count; ++j){
			int id = DaoByteDecoder_DecodeUInt16( self );
			int id2 = DaoByteDecoder_DecodeUInt16( self );
			DaoType *type = self->types->items.pType[id2-1];
			DMap_Insert( routine->body->localVarType, IntToPointer(id), type );
		}
		count = DaoByteDecoder_DecodeUInt16( self );
		printf( "codes %3i\n", count );
		for(j=0; j<count; ++j){
			DaoVmCodeX vmc = {0,0,0,0,0,0,0,0,0};
			vmc.code = DaoByteDecoder_DecodeUInt16( self );
			vmc.a = DaoByteDecoder_DecodeUInt16( self );
			vmc.b = DaoByteDecoder_DecodeUInt16( self );
			vmc.c = DaoByteDecoder_DecodeUInt16( self );
			vmc.level = DaoByteDecoder_DecodeUInt16( self );
			vmc.line = DaoByteDecoder_DecodeUInt16( self );
			DArray_Append( routine->body->annotCodes, & vmc );
			DaoVmcArray_PushBack( routine->body->vmCodes, * (DaoVmCode*) & vmc );
		}
		printf( "<<<<<<%p %p\n", routine, routine->routType );
		//DaoRoutine_PrintCode( routine, self->vmspace->stdioStream );
		DaoInferencer_Init( inferencer, routine, 0 );
		if( DaoInferencer_DoInference( inferencer ) == 0 ){
			DaoInferencer_Delete( inferencer );
			self->codes = self->error;
			break;
		}
		DaoInferencer_Delete( inferencer );
	}
}

int DaoByteDecoder_Decode( DaoByteDecoder *self, DString *input, DaoNamespace *nspace )
{
	if( strncmp( input->mbs, DAO_BC_SIGNATURE, 8 ) != 0 ) return 0;
	if( input->mbs[8] != 0 ) return 0; /* Not official format; */

	self->nspace = nspace;
	self->codes = (uchar_t*) input->mbs + 16;
	self->end = (uchar_t*) input->mbs + input->size;
	self->error = self->end + 1;
	DaoByteDecoder_DecodeShortString( self, self->string );

	DaoByteDecoder_DecodeIdentifiers( self );
	DaoByteDecoder_DecodeModules( self );
	DaoByteDecoder_DecodeDeclarations( self );
	DaoByteDecoder_DecodeTypes( self );
	DaoByteDecoder_DecodeValues( self );
	DaoByteDecoder_DecodeConstants( self );
	DaoByteDecoder_DecodeVariables( self );
	DaoByteDecoder_DecodeGlobalTypes( self );
	DaoByteDecoder_DecodeInterfaces( self );
	DaoByteDecoder_DecodeClasses( self );
	DaoByteDecoder_DecodeRoutines( self );

	printf( "%p %p\n", self->codes, self->end );
	return self->codes <= self->end;
}
