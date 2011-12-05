/*=========================================================================================
  This file is a part of the Dao standard modules.
  Copyright (C) 2011, Fu Limin. Email: fu@daovm.net, limin.fu@yahoo.com

  This software is free software; you can redistribute it and/or modify it under the terms 
  of the GNU Lesser General Public License as published by the Free Software Foundation; 
  either version 2.1 of the License, or (at your option) any later version.

  This software is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
  See the GNU Lesser General Public License for more details.
  =========================================================================================*/

#include<stdlib.h>
#include<string.h>
#include<math.h>
#include"daoString.h"
#include"daoValue.h"
#include"daoParser.h"
#include"daoNamespace.h"
#include"daoGC.h"
#include"dao_aux.h"

DAO_INIT_MODULE


#define RADIX 32
static const char *hex_digits = "ABCDEFGHIJKLMNOP";
static const char *mydigits = "0123456789ABCDEFGHIJKLMNOPQRSTUVW";

void DaoEncodeInteger( char *p, dint value )
{
	int m;
	if( value < 0 ){
		*(p++) = '-';
		value = - value;
	}
	*(p++) = 'X';
	*p = 0;
	if( value == 0 ){
		*(p++) = '0';
		*p = 0;
		return;
	}
	while( value ){
		m = value % RADIX;
		value /= RADIX;
		*(p++) = mydigits[m];
	}
	*p = 0;
}
dint DaoDecodeInteger( char *p )
{
	dint value = 0;
	dint power = 1;
	int sign = 1;
	if( *p == '-' ){
		sign = -1;
		p ++;
	}
	if( *p == 'X' ) p ++;
	while( *p ){
		int digit = *p;
		digit -= digit >= 'A' ? 'A' - 10 : '0';
		value += digit * power;
		power *= RADIX;
		p ++;
	}
	return value * sign;
}

void DaoEncodeDouble( char *buf, double value )
{
	int expon, digit;
	double prod, frac;
	char *p = buf;
	if( value <0.0 ){
		*(p++) = '-';
		value = -value;
	}
	*(p++) = 'X';
	*p = 0;
	frac = frexp( value, & expon );
	while(1){
		prod = frac * RADIX;
		digit = (int) prod;
		frac = prod - digit;
		*(p++) = mydigits[ digit ];
		if( frac <= 0 ) break;
	}
	*(p++) = '_';
	if( expon < 0 ) *(p++) = '_';
	DaoEncodeInteger( p, abs( expon ) );
	return;
}
double DaoDecodeDouble( char *buf )
{
	double frac = 0;
	int expon, sign = 1, sign2 = 1;
	char *p = buf;
	double factor = 1.0 / RADIX;
	double accum = factor;
	if( buf[0] == '-' ){
		p ++;
		sign = -1;
	}
	if( *p == 'X' ) p ++;
	while( *p && *p != '_' ){
		int digit = *p;
		digit -= digit >= 'A' ? 'A' - 10 : '0';
		frac += accum * digit;
		accum *= factor;
		p ++;
	}
	if( p[1] == '_' ){
		sign2 = -1;
		p ++;
	}
	expon = sign2 * DaoDecodeInteger( p+1 );
	return ldexp( frac, expon ) * sign;
}

static void DaoSerializeInteger( dint value, DString *serial )
{
	char buf[100];
	DaoEncodeInteger( buf, value );
	DString_AppendMBS( serial, buf );
}
static void DaoSerializeDouble( double value, DString *serial )
{
	char buf[100];
	DaoEncodeDouble( buf, value );
	DString_AppendMBS( serial, buf );
}
static void DaoSerializeComplex( complex16 value, DString *serial )
{
	DaoSerializeDouble( value.real, serial );
	DString_AppendChar( serial, ' ' );
	DaoSerializeDouble( value.imag, serial );
}
static void DaoSerializeLong( DLong *value, DString *serial )
{
	int i;
	DaoSerializeInteger( value->base, serial );
	DString_AppendChar( serial, value->sign > 0 ? '+' : '-' );
	for(i=0; i<value->size; i++){
		if( i ) DString_AppendChar( serial, ',' );
		DaoSerializeInteger( value->data[i], serial );
	}
}

static int DaoValue_Serialize2( DaoValue*, DString*, DaoNamespace*, DaoProcess*, DaoType*, DString* );

static void DString_Serialize( DString *self, DString *serial, DString *buf )
{
	int i;
	unsigned char *mbs;

	DString_Clear( buf );
	DString_ToMBS( buf );
	DString_Append( buf, self );
	mbs = (unsigned char*) buf->mbs;
	DString_AppendChar( serial, self->mbs ? '\'' : '\"' );
	for(i=0; i<buf->size; i++){
		DString_AppendChar( serial, hex_digits[ mbs[i] / 16 ] );
		DString_AppendChar( serial, hex_digits[ mbs[i] % 16 ] );
	}
	DString_AppendChar( serial, self->mbs ? '\'' : '\"' );
}
static void DaoArray_Serialize( DaoArray *self, DString *serial, DString *buf )
{
	DaoInteger intmp = {DAO_INTEGER,0,0,0,0,0};
	DaoValue *value = (DaoValue*) & intmp;
	int i;
	DString_AppendChar( serial, '[' );
	for(i=0; i<self->ndim; i++){
		value->xInteger.value = self->dims[i];
		if( i ) DString_AppendChar( serial, ',' );
		DaoValue_GetString( value, buf );
		DString_Append( serial, buf );
	}
	DString_AppendChar( serial, ']' );
	for(i=0; i<self->size; i++){
		if( i ) DString_AppendChar( serial, ',' );
		switch( self->etype ){
		case DAO_INTEGER : DaoSerializeInteger( self->data.i[i], serial ); break;
		case DAO_FLOAT : DaoSerializeDouble( self->data.f[i], serial ); break;
		case DAO_DOUBLE : DaoSerializeDouble( self->data.d[i], serial ); break;
		case DAO_COMPLEX : DaoSerializeComplex( self->data.c[i], serial ); break;
		}
	}
}
static int DaoList_Serialize( DaoList *self, DString *serial, DaoNamespace *ns, DaoProcess *proc, DString *buf )
{
	DaoType *type = self->unitype;
	int i, rc = 1;
	if( type->nested && type->nested->size ) type = type->nested->items.pType[0];
	if( type && type->simtype == 0 && (type->tid == 0 || type->tid >= DAO_ENUM)) type = NULL;
	for(i=0; i<self->items.size; i++){
		DaoType *it = NULL;
		if( type == NULL ) it = DaoNamespace_GetType( ns, self->items.items.pValue[i] );
		if( i ) DString_AppendChar( serial, ',' );
		if( it == NULL && type && type->tid >= DAO_ARRAY ) DString_AppendChar( serial, '{' );
		rc &= DaoValue_Serialize2( self->items.items.pValue[i], serial, ns, proc, it, buf );
		if( it == NULL && type && type->tid >= DAO_ARRAY ) DString_AppendChar( serial, '}' );
	}
	return rc;
}
static int DaoMap_Serialize( DaoMap *self, DString *serial, DaoNamespace *ns, DaoProcess *proc, DString *buf )
{
	DaoType *type = self->unitype;
	DaoType *keytype = NULL;
	DaoType *valtype = NULL;
	DNode *node;
	char *sep = self->items->hashing ? ":" : "=>";
	int i = 0, rc = 1;
	if( type->nested && type->nested->size >0 ) keytype = type->nested->items.pType[0];
	if( type->nested && type->nested->size >1 ) valtype = type->nested->items.pType[1];
	if( keytype && (keytype->tid == 0 || keytype->tid >= DAO_ENUM)) keytype = NULL;
	if( valtype && (valtype->tid == 0 || valtype->tid >= DAO_ENUM)) valtype = NULL;
	for(node=DMap_First(self->items); node; node=DMap_Next(self->items,node)){
		DaoType *kt = NULL, *vt = NULL;
		if( keytype == NULL ) kt = DaoNamespace_GetType( ns, node->key.pValue );
		if( valtype == NULL ) vt = DaoNamespace_GetType( ns, node->value.pValue );
		if( (i++) ) DString_AppendChar( serial, ',' );
		rc &= DaoValue_Serialize2( node->key.pValue, serial, ns, proc, kt, buf );
		DString_AppendMBS( serial, sep );
		rc &= DaoValue_Serialize2( node->value.pValue, serial, ns, proc, vt, buf );
	}
	return rc;
}
static int DaoTuple_Serialize( DaoTuple *self, DString *serial, DaoNamespace *ns, DaoProcess *proc, DString *buf )
{
	DArray *nested = self->unitype ? self->unitype->nested : NULL;
	int i, rc = 1;
	for(i=0; i<self->size; i++){
		DaoType *type = NULL;
		DaoType *it = NULL;
		if( nested && nested->size > i ) type = nested->items.pType[i];
		if( type && type->tid == DAO_PAR_NAMED ) type = & type->aux->xType;
		if( type && (type->tid == 0 || type->tid >= DAO_ENUM)) type = NULL;
		if( type == NULL ) it = DaoNamespace_GetType( ns, self->items[i] );
		if( i ) DString_AppendChar( serial, ',' );
		rc &= DaoValue_Serialize2( self->items[i], serial, ns, proc, it, buf );
	}
	return rc;
}
static int DaoObject_Serialize( DaoObject *self, DString *serial, DaoNamespace *ns, DaoProcess *proc, DString *buf )
{
	DaoType *type;
	DaoValue *value = NULL;
	DaoValue *selfpar = (DaoValue*) self;
	DString name = DString_WrapMBS( "serialize" );
	int errcode = DaoObject_GetData( self, & name, & value, NULL );
	if( errcode || value == NULL || value->type < DAO_FUNCTREE || value->type > DAO_FUNCTION ) return 0;
	if( DaoProcess_Call( proc, (DaoMethod*)value, selfpar, NULL, 0 ) ) return 0;
	type = DaoNamespace_GetType( ns, proc->stackValues[0] );
	DaoValue_Serialize2( proc->stackValues[0], serial, ns, proc, type, buf );
	return 1;
}
static int DaoCdata_Serialize( DaoCdata *self, DString *serial, DaoNamespace *ns, DaoProcess *proc, DString *buf )
{
	DaoType *type;
	DaoValue *meth;
	if( self->typer == & cdataTyper ){
		uint_t i;
		for(i=0; i<self->size; i++){
			DString_AppendChar( serial, hex_digits[ self->buffer.pUChar[i] / 16 ] );
			DString_AppendChar( serial, hex_digits[ self->buffer.pUChar[i] % 16 ] );
		}
		return 1;
	}
	if( (meth = DaoTypeBase_FindFunctionMBS( self->typer, "serialize" )) == NULL ) return 0;
	if( DaoProcess_Call( proc, (DaoMethod*)meth, (DaoValue*)self, NULL, 0 ) ) return 0;
	type = DaoNamespace_GetType( ns, proc->stackValues[0] );
	DaoValue_Serialize2( proc->stackValues[0], serial, ns, proc, type, buf );
	return 1;
}
int DaoValue_Serialize2( DaoValue *self, DString *serial, DaoNamespace *ns, DaoProcess *proc, DaoType *type, DString *buf )
{
	int rc = 1;
	if( type ){
		DString_Append( serial, type->name );
		DString_AppendChar( serial, '{' );
	}
	switch( self->type ){
	case DAO_NONE :
		break;
	case DAO_INTEGER :
		DaoSerializeInteger( self->xInteger.value, serial );
		break;
	case DAO_FLOAT :
		DaoSerializeDouble( self->xFloat.value, serial );
		break;
	case DAO_DOUBLE :
		DaoSerializeDouble( self->xDouble.value, serial );
		break;
	case DAO_COMPLEX :
		DaoSerializeComplex( self->xComplex.value, serial );
		break;
	case DAO_LONG :
		DaoSerializeLong( self->xLong.value, serial );
		break;
	case DAO_STRING :
		DString_Serialize( self->xString.data, serial, buf );
		break;
	case DAO_ENUM :
		DaoSerializeInteger( self->xEnum.value, serial );
		break;
	case DAO_ARRAY :
		DaoArray_Serialize( & self->xArray, serial, buf );
		break;
	case DAO_LIST :
		rc = DaoList_Serialize( & self->xList, serial, ns, proc, buf );
		break;
	case DAO_MAP :
		rc = DaoMap_Serialize( & self->xMap, serial, ns, proc, buf );
		break;
	case DAO_TUPLE :
		rc = DaoTuple_Serialize( & self->xTuple, serial, ns, proc, buf );
		break;
	case DAO_OBJECT :
		if( proc == NULL ) break;
		rc = DaoObject_Serialize( & self->xObject, serial, ns, proc, buf );
		break;
	case DAO_CDATA :
		if( proc == NULL ) break;
		rc = DaoCdata_Serialize( & self->xCdata, serial, ns, proc, buf );
		break;
	default :
		DString_AppendChar( serial, '?' );
		rc = 0;
		break;
	}
	if( type ) DString_AppendChar( serial, '}' );
	return rc;
}
int DaoValue_Serialize( DaoValue *self, DString *serial, DaoNamespace *ns, DaoProcess *proc )
{
	DaoType *type = DaoNamespace_GetType( ns, self );
	DString *buf = DString_New(1);
	int rc;
	DString_Clear( serial );
	DString_ToMBS( serial );
	rc = DaoValue_Serialize2( self, serial, ns, proc, type, buf );
	DString_Delete( buf );
	return rc;
}

int DaoParser_FindPairToken( DaoParser *self,  uchar_t lw, uchar_t rw, int start, int stop );
DaoType* DaoParser_ParseType( DaoParser *self, int start, int end, int *newpos, DArray *types );

static DaoObject* DaoClass_MakeObject( DaoClass *self, DaoValue *param, DaoProcess *proc )
{
	DaoValue *cst = (DaoValue*)self->classRoutines;
	DaoObject *object = DaoObject_New( self );
	if( DaoProcess_PushCallable( proc, cst, (DaoValue*)object, & param, 1 ) ==0 ){
		proc->topFrame->returning = -1;
		if( DaoProcess_Execute( proc ) ) return object;
	}
	GC_IncRC( object ); GC_DecRC( object );
	return NULL;
}
static DaoCdata* DaoCdata_MakeObject( DaoCdata *self, DaoValue *param, DaoProcess *proc )
{
	DaoValue *value = DaoTypeBase_FindFunction( self->typer, self->ctype->name );
	if( DaoProcess_PushCallable( proc, value, NULL, & param, 1 ) ) return NULL;
	proc->topFrame->active = proc->firstFrame;
	DaoProcess_SetActiveFrame( proc, proc->firstFrame ); /* return value in stackValues[0] */
	if( DaoProcess_Execute( proc ) == 0 ) return NULL;
	value = proc->stackValues[0];
	if( value && value->type == DAO_CDATA ) return & value->xCdata;
	return NULL;
}

static int DaoParser_Deserialize( DaoParser *self, int start, int end, DaoValue **value2, DArray *types, DaoNamespace *ns, DaoProcess *proc )
{
	DaoToken **tokens = self->tokens->items.pToken;
	DaoType *it1 = NULL, *it2 = NULL, *type = NULL;
	DaoValue *value = *value2;
	DaoValue *tmp = NULL;
	DaoValue *tmp2 = NULL;
	DaoObject *object;
	DaoCdata *cdata;
	DaoArray *array;
	DaoTuple *tuple;
	DaoList *list;
	DaoMap *map;
	DArray *dims;
	DNode *node;
	char *str;
	int i, j, k, n;
	int minus = 0;
	int next = start + 1;
	int tok2 = start < end ? tokens[start+1]->type : 0;
	int maybetype = tok2 == DTOK_COLON2 || tok2 == DTOK_LT || tok2 == DTOK_LCB;

	if( tokens[start]->name == DTOK_ID_SYMBOL ){
		DString *mbs = DString_New(1);
		while( tokens[start]->name == DTOK_ID_SYMBOL ){
			DString_Append( mbs, tokens[start]->string );
			start += 1;
		}
		type = DaoNamespace_MakeType( ns, mbs->mbs, DAO_ENUM, NULL, NULL, 0 );
		DString_Delete( mbs );
		if( type == NULL ) return start;
		if( tokens[start]->name != DTOK_LCB ) return start;
		end = DaoParser_FindPairToken( self, DTOK_LCB, DTOK_RCB, start, end );
		if( end < 0 ) return start;
		next = end + 1;
		start += 1;
		end -= 1;
	}else if( tokens[start]->type == DTOK_IDENTIFIER && maybetype ){
		type = DaoParser_ParseType( self, start, end, & start, NULL );
		if( type == NULL ) return next;
		if( tokens[start]->name != DTOK_LCB ) return start;
		end = DaoParser_FindPairToken( self, DTOK_LCB, DTOK_RCB, start, end );
		if( end < 0 ) return start;
		next = end + 1;
		start += 1;
		end -= 1;
	}
	if( type == NULL ){
		type = types->items.pType[0];
		if( type && type->tid >= DAO_ARRAY ){
			if( tokens[start]->name != DTOK_LCB ) return start;
			end = DaoParser_FindPairToken( self, DTOK_LCB, DTOK_RCB, start, end );
			if( end < 0 ) return start;
			next = end + 1;
			start += 1;
			end -= 1;
		}
	}
	if( type == NULL ) return next;
	DaoValue_Copy( type->value, value2 );
	if( start > end ) return next;
	if( tokens[start]->name == DTOK_SUB ){
		minus = 1;
		start += 1;
		if( start > end ) return next;
	}
	if( type->nested && type->nested->size >0 ) it1 = type->nested->items.pType[0];
	if( type->nested && type->nested->size >1 ) it2 = type->nested->items.pType[1];
	str = tokens[start]->string->mbs;
#if 0
	printf( "type: %s %s\n", type->name->mbs, str );
	for(i=start; i<=end; i++) printf( "%s ", tokens[i]->string->mbs ); printf( "\n" );
#endif
	value = *value2;
	switch( type->tid ){
	case DAO_NONE :
		break;
	case DAO_INTEGER :
		value->xInteger.value = DaoDecodeInteger( str );
		if( minus ) value->xInteger.value = - value->xInteger.value;
		break;
	case DAO_FLOAT :
		value->xFloat.value = DaoDecodeDouble( str );
		if( minus ) value->xFloat.value = - value->xFloat.value;
		break;
	case DAO_DOUBLE :
		value->xDouble.value = DaoDecodeDouble( str );
		if( minus ) value->xDouble.value = - value->xDouble.value;
		break;
	case DAO_COMPLEX :
		value->xComplex.value.real = DaoDecodeDouble( str );
		if( minus ) value->xComplex.value.real = - value->xComplex.value.real;
		if( start + 1 > end ) return start+1;
		minus = 0;
		if( tokens[start + 1]->name == DTOK_SUB ){
			minus = 1;
			start += 1;
			if( start + 1 > end ) return start+1;
		}
		value->xComplex.value.imag = DaoDecodeDouble( tokens[start+1]->string->mbs );
		if( minus ) value->xComplex.value.imag = - value->xComplex.value.imag;
		next = start + 2;
		break;
	case DAO_LONG :
		value->xLong.value->base = DaoDecodeInteger( str );
		start += 1;
		if( tokens[start]->name == DTOK_ADD ){
			value->xLong.value->sign = 1;
			start += 1;
		}else if( tokens[start]->name == DTOK_SUB ){
			value->xLong.value->sign = -1;
			start += 1;
		}
		for(i=start; i<=end; i++){
			if( tokens[i]->name == DTOK_COMMA ) continue;
			DLong_PushBack( value->xLong.value, DaoDecodeInteger( tokens[i]->string->mbs ) );
		}
		break;
	case DAO_STRING :
		n = tokens[start]->string->size - 1;
		for(i=1; i<n; i++){
			char c1 = str[i];
			char c2 = str[i+1];
			if( c1 < 'A' || c1 > 'P' ) continue;
			DString_AppendChar( value->xString.data, (char)((c1-'A')*16 + (c2-'A')) );
			i += 1;
		}
		if( str[0] == '\"' ) DString_ToWCS( value->xString.data );
		break;
	case DAO_ENUM :
		value->xEnum.value = DaoDecodeInteger( str );
		break;
	case DAO_ARRAY :
#ifdef DAO_WITH_NUMARRAY
		if( tokens[start]->name != DTOK_LSB ) return next;
		k = DaoParser_FindPairToken( self, DTOK_LSB, DTOK_RSB, start, end );
		if( k < 0 ) return next;
		n = 1;
		for(i=start+1; i<k; i++){
			if( tokens[i]->name == DTOK_COMMA ) continue;
			n *= strtol( tokens[i]->string->mbs, 0, 0 );
		}
		if( n < 0 ) return next;
		if( it1 == NULL || it1->tid == 0 || it1->tid > DAO_COMPLEX ) return next;
		array = & value->xArray;
		dims = DArray_New(0);
		for(i=start+1; i<k; i++){
			if( tokens[i]->name == DTOK_COMMA ) continue;
			j = strtol( tokens[i]->string->mbs, 0, 0 );
			DArray_Append( dims, (size_t) j );
		}
		n = dims->size;
		DaoArray_ResizeArray( array, dims->items.pSize, n );
		DArray_PushFront( types, it1 );
		DArray_Delete( dims );
		n = 0;
		for(i=k+1; i<=end; i++){
			j = i + 1;
			while( j <= end && tokens[j]->name != DTOK_COMMA ) j += 1;
			DaoParser_Deserialize( self, i, j-1, & tmp, types, ns, proc );
			switch( it1->tid ){
			case DAO_INTEGER : array->data.i[n] = tmp->xInteger.value; break;
			case DAO_FLOAT   : array->data.f[n] = tmp->xFloat.value; break;
			case DAO_DOUBLE  : array->data.d[n] = tmp->xDouble.value; break;
			}
			i = j;
			n += 1;
		}
		DArray_PopFront( types );
#endif
		break;
	case DAO_LIST :
		list = & value->xList;
		DArray_PushFront( types, it1 );
		n = 0;
		for(i=start; i<=end; i++){
			if( tokens[i]->name == DTOK_COMMA ) continue;
			DArray_Append( & list->items, NULL );
			k = DaoParser_Deserialize( self, i, end, list->items.items.pValue + n, types, ns, proc );
			i = k - 1;
			n += 1;
		}
		DArray_PopFront( types );
		break;
	case DAO_MAP :
		map = & value->xMap;
		n = 0;
		for(i=start; i<=end; i++){
			if( tokens[i]->name == DTOK_COMMA ) continue;
			DaoValue_Clear( & tmp );
			DaoValue_Clear( & tmp2 );
			DArray_PushFront( types, it1 );
			i = DaoParser_Deserialize( self, i, end, &tmp, types, ns, proc );
			DArray_PopFront( types );
			if( tokens[i]->name == DTOK_COMMA ) continue;
			if( map->items->size == 0 ){
				if( tokens[i]->name == DTOK_COLON ){
					DMap_Delete( map->items );
					map->items = DHash_New( D_VALUE, D_VALUE );
				}
			}
			if( tokens[i]->name == DTOK_COLON || tokens[i]->name == DTOK_FIELD ) i += 1;
			DArray_PushFront( types, it2 );
			i = DaoParser_Deserialize( self, i, end, &tmp2, types, ns, proc );
			DArray_PopFront( types );
			node = DMap_Insert( map->items, (void*) tmp, (void*) tmp2 );
			i -= 1;
			n += 1;
		}
		break;
	case DAO_TUPLE :
		tuple = & value->xTuple;
		n = 0;
		for(i=start; i<=end; i++){
			if( tokens[i]->name == DTOK_COMMA ) continue;
			it1 = NULL;
			if( type->nested && type->nested->size > n ){
				it1 = type->nested->items.pType[n];
				if( it1 && it1->tid == DAO_PAR_NAMED ) it1 = & it1->aux->xType;
			}
			DArray_PushFront( types, it1 );
			i = DaoParser_Deserialize( self, i, end, tuple->items + n, types, ns, proc );
			DArray_PopFront( types );
			i -= 1;
			n += 1;
		}
		break;
	case DAO_OBJECT :
		DArray_PushFront( types, NULL );
		DaoParser_Deserialize( self, start, end, & tmp, types, ns, proc );
		DArray_PopFront( types );
		if( tmp == NULL ) break;
		object = DaoClass_MakeObject( & type->aux->xClass, tmp, proc );
		if( object == NULL ) break;
		value = (DaoValue*) object;
		DaoValue_Copy( value, value2 );
		if( *value2 != value ) GC_IncRC( value ), GC_DecRC( value );
		break;
	case DAO_CDATA :
		DArray_PushFront( types, NULL );
		DaoParser_Deserialize( self, start, end, & tmp, types, ns, proc );
		DArray_PopFront( types );
		if( tmp == NULL ) break;
		cdata = DaoCdata_MakeObject( & type->aux->xCdata, tmp, proc );
		if( cdata == NULL ) break;
		value = (DaoValue*) cdata;
		DaoValue_Copy( value, value2 );
		if( *value2 != value ) GC_IncRC( value ), GC_DecRC( value );
		break;
	}
	DaoValue_Clear( & tmp );
	DaoValue_Clear( & tmp2 );
	return next;
}
int DaoValue_Deserialize( DaoValue **self, DString *serial, DaoNamespace *ns, DaoProcess *proc )
{
	DaoParser *parser = DaoParser_New();
	DArray *types = DArray_New(0);
	int rc;

	DaoValue_Clear( self );
	parser->nameSpace = ns;
	parser->vmSpace = ns->vmSpace;
	DaoParser_LexCode( parser, DString_GetMBS( serial ), 0 );
	if( parser->tokens->size == 0 ) goto Failed;

	DArray_PushFront( types, NULL );
	rc = DaoParser_Deserialize( parser, 0, parser->tokens->size-1, self, types, ns, proc );

	DaoParser_Delete( parser );
	DArray_Delete( types );
	return rc;
Failed:
	DaoParser_Delete( parser );
	DArray_Delete( types );
	return 0;
}


static void NS_Backup( DaoNamespace *self, DaoProcess *proc, FILE *fout, int limit, int store )
{
	DNode *node = DMap_First( self->lookupTable );
	DString *prefix = DString_New(1);
	DString *serial = DString_New(1);
	DaoValue *value = NULL;
	size_t max = limit * 1000; /* limit per object in KB */
	int id, pm, up, st;

	for( ; node !=NULL; node = DMap_Next( self->lookupTable, node ) ){
		DString *name = node->key.pString;
		id = node->value.pSize;
		up = LOOKUP_UP( id );
		st = LOOKUP_ST( id );
		pm = LOOKUP_PM( id );
		if( up ) continue;
		if( st != store ) continue;
		if( st == DAO_GLOBAL_CONSTANT ) value = DaoNamespace_GetConst( self, id );
		if( st == DAO_GLOBAL_VARIABLE ) value = DaoNamespace_GetVariable( self, id );
		if( value == NULL ) continue;
		if( DaoValue_Serialize( value, serial, self, proc ) ==0 ) continue;
		prefix->size = 0;
		switch( pm ){
		case DAO_DATA_PRIVATE   : DString_AppendMBS( prefix, "private " ); break;
		case DAO_DATA_PROTECTED : DString_AppendMBS( prefix, "protected " ); break;
		case DAO_DATA_PUBLIC    : DString_AppendMBS( prefix, "public " ); break;
		}
		switch( st ){
		case DAO_GLOBAL_CONSTANT : DString_AppendMBS( prefix, "const " ); break;
		case DAO_GLOBAL_VARIABLE : DString_AppendMBS( prefix, "var " ); break;
		}
		if( max && prefix->size + name->size + serial->size + 4 > max ) continue;
		fprintf( fout, "%s%s = %s\n", prefix->mbs, name->mbs, serial->mbs );
	}
	DString_Delete( prefix );
	DString_Delete( serial );
}
void DaoNamespace_Backup( DaoNamespace *self, DaoProcess *proc, FILE *fout, int limit )
{
	int i;
	NS_Backup( self, proc, fout, limit, DAO_GLOBAL_CONSTANT );
	if( self->inputs->size ){
		static const char *digits = "ABCDEFGHIJKLMNOP";
		unsigned char *mbs = (unsigned char*) self->inputs->mbs;
		fprintf( fout, "require { " );
		for(i=0; i<self->inputs->size; i++){
			fprintf( fout, "%c%c", digits[ mbs[i] / 16 ], digits[ mbs[i] % 16 ] );
		}
		fprintf( fout, " }\n" );
	}
	NS_Backup( self, proc, fout, limit, DAO_GLOBAL_VARIABLE );
}
void DaoNamespace_Restore( DaoNamespace *self, DaoProcess *proc, FILE *fin )
{
	DaoParser *parser = DaoParser_New();
	DString *line = DString_New(1);
	DArray *types = DArray_New(0);
	DArray *tokens = parser->tokens;
	DString *name;
	DNode *node;

	parser->nameSpace = self;
	parser->vmSpace = self->vmSpace;
	while( DaoFile_ReadLine( fin, line ) ){
		DaoValue *value = NULL;
		int st = DAO_GLOBAL_VARIABLE;
		int pm = DAO_DATA_PRIVATE;
		int i, n, start = 0;
		char *mbs;

		DaoParser_LexCode( parser, line->mbs, 0 );
		if( tokens->size == 0 ) continue;
		switch( tokens->items.pToken[start]->name ){
		case DKEY_PRIVATE   : pm = DAO_DATA_PRIVATE;   start += 1; break;
		case DKEY_PROTECTED : pm = DAO_DATA_PROTECTED; start += 1; break;
		case DKEY_PUBLIC    : pm = DAO_DATA_PUBLIC;    start += 1; break;
		}
		if( start >= tokens->size ) continue;
		switch( tokens->items.pToken[start]->name ){
		case DKEY_CONST : st = DAO_GLOBAL_CONSTANT; start += 1; break;
		case DKEY_VAR   : st = DAO_GLOBAL_VARIABLE; start += 1; break;
		}
		if( tokens->items.pToken[start]->name != DTOK_IDENTIFIER ) continue;
		name = tokens->items.pToken[start]->string;
		start += 1;
		if( start + 3 >= tokens->size ) continue;
		if( tokens->items.pToken[start]->name != DTOK_ASSN ) continue;
		start += 1;

		DArray_Clear( parser->errors );
		DArray_Clear( types );
		DArray_PushFront( types, NULL );
		DaoParser_Deserialize( parser, start, tokens->size-1, &value, types, self, proc );
		if( value == NULL ) continue;
		node = DMap_Find( self->lookupTable, name );
		if( node ) continue;
		if( st == DAO_GLOBAL_CONSTANT ){
			DaoNamespace_AddConst( self, name, value, pm );
		}else{
			DaoNamespace_AddVariable( self, name, value, NULL, pm );
		}
		GC_DecRC( value );
	}
	DString_Delete( line );
	DArray_Delete( types );
	DaoParser_Delete( parser );
}

static void AUX_Serialize( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *mbs = DaoProcess_PutMBString( proc, "" );
	DaoValue_Serialize( p[0], mbs, proc->activeNamespace, proc );
}
static void AUX_Deserialize( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoValue *value = NULL;
	DaoValue_Deserialize( & value, p[0]->xString.data, proc->activeNamespace, proc );
	DaoProcess_PutValue( proc, value );
	GC_DecRC( value );
}
static void AUX_Backup( DaoProcess *proc, DaoValue *p[], int N )
{
	FILE *fout = fopen( DString_GetMBS( p[0]->xString.data ), "w+" );
	if( fout == NULL ){
		DaoProcess_RaiseException( proc,DAO_ERROR_FILE, DString_GetMBS( p[0]->xString.data ) );
		return;
	}
	DaoNamespace_Backup( proc->activeNamespace, proc, fout, p[1]->xInteger.value );
	fclose( fout );
}
static void AUX_Restore( DaoProcess *proc, DaoValue *p[], int N )
{
	FILE *fin = fopen( DString_GetMBS( p[0]->xString.data ), "r" );
	if( fin == NULL ){
		DaoProcess_RaiseException( proc,DAO_ERROR_FILE, DString_GetMBS( p[0]->xString.data ) );
		return;
	}
	DaoNamespace_Restore( proc->activeNamespace, proc, fin );
	fclose( fin );
}
static void AUX_Tokenize( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *source = p[0]->xString.data;
	DaoList *list = DaoProcess_PutList( proc );
	DArray *tokens = DArray_New(D_TOKEN);
	int i, rc = 0;
	DString_ToMBS( source );
	rc = DaoToken_Tokenize( tokens, source->mbs, 0, 1, 1 );
	if( rc ){
		DaoString *str = DaoString_New(1);
		for(i=0; i<tokens->size; i++){
			DString_Assign( str->data, tokens->items.pToken[i]->string );
			DArray_Append( & list->items, (DaoValue*) str );
		}
		DaoString_Delete( str );
	}
	DArray_Delete( tokens );
}
static void AUX_Log( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *info = p[0]->xString.data;
	FILE *fout = fopen( "dao.log", "a" );
	DString_ToMBS( info );
	fprintf( fout, "%s\n", info->mbs );
	fclose( fout );
}

static DaoFuncItem auxMeths[]=
{
	{ AUX_Serialize,   "serialize( value : any )=>string" },
	{ AUX_Deserialize, "deserialize( text : string )=>any" },
	{ AUX_Backup,      "backup( tofile = 'backup.sdo', limit=0 )" },
	{ AUX_Restore,     "restore( fromfile = 'backup.sdo' )" },
	{ AUX_Tokenize,    "tokenize( source :string )=>list<string>" },
	{ AUX_Log,         "log( info='' )" },
	{ NULL, NULL }
};

static DaoTypeBase auxTyper = { "aux", NULL, NULL, auxMeths, {0}, {0}, NULL, NULL };

int DaoOnLoad( DaoVmSpace *vmSpace, DaoNamespace *ns )
{
	DaoNamespace_WrapType( ns, & auxTyper );
	return 0;
}
