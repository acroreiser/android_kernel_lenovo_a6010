/////////////////////////////////////////////////////////////////////////////
// File Name	: OIS_misc.c
// Function		: Misccellaneous function.
// Rule         : Use TAB 4
//
// Copyright(c)	Rohm Co.,Ltd. All rights reserved
//
/***** ROHM Confidential ***************************************************/
#ifndef OIS_MISC_C
#define OIS_MISC_C
#endif
#if 0

//#include <windows.h>
//#include <wingdi.h>
//#include <stdlib.h>
#include <linux/module.h>
#include <linux/kernel.h>
//#include <malloc.h>
#include "OIS_head.h"
//#include "usb_func.h"

// /////////////////////////////////////////////////////////
// Linear or quadratic regression
// ---------------------------------------------------------
// <Function>
//		This function calculates linear or quadratic regression parameters.
//
// <Input>
//		OIS_UWORD	mode		0 = Linear regression		(y = 		bx + c)
// 								1 = Quadratic regression	(y = ax^2 + bx + c
//		OIS_WORD	inpCount	Total number of x and y data.
//		double		*inp		Input pointer of OIS_WORD type data array for approximation.
// 									(x, y) array are single dimension array as follows:
// 										inp[] = {x_0, y_0, x_1, y_1, ... , x_n, y_n, x_n+1, y_n+1, ..}
// 		double		*ret		Output pointer of results array.
// 									ret[0] = Reserved
// 									ret[1] = a
// 									ret[2] = b
// 									ret[3] = c
// 									ret[4] = Correlation cofficient
//
// 									where approximation is as follows:
// 										y = ax^2 + bx + c	(a = 0 when linear approximation is selected)
//
// <Output>
//		none
//
// <Description>
//
// =========================================================
void    Approx(OIS_UWORD mode, OIS_WORD inpCount, double *y, double *ret)
{
	OIS_WORD i, n;
	double a, b, c, d, e, f, g;
	double num;
	double dat[6];
	double a_dat, b_dat, c_dat;
	double R_DAT, Q_DAT;
// 	double *y;

	// Memory Allocation
	//----------------------------------------------
// 	if((y = malloc(inpCount * sizeof(double))) == NULL){
// 		DEBUG_printf(("Memory allocation error for Filterling."));
// 		exit(-1);
// 		return;
// 	}

	// Initialize variables
	//----------------------------------------------
	a = b = c = d = e = f = g = 0.0;
	n = inpCount / 2;
	num = (double)n;

	// Subsutitute y[] for inp[]
// 	for (i = 0; i < inpCount; i++)
// 	{
// 		y[i] = (inp[i + 1]);
// 	}

	// Calculation
	//----------------------------------------------
	for (i = 0; i < n; i++)
	{
		dat[0] = y[2 * i]; a += dat[0];			    // [Sigma]xj		( xn, yn ) = ( y[2n], y[2n+1] )
		e += y[2 * i + 1];	        				// [Sigma]     yj
		f += (dat[0] * y[2 * i + 1]);				// [Sigma]xj  *yj
		dat[0] *= y[2 * i]; b += dat[0];			// [Sigma]xj^2
		g += (dat[0] * y[2 * i + 1]);				// [Sigma]xj^2*yj
		dat[0] *= y[2 * i]; c += dat[0];			// [Sigma]xj^3
		dat[0] *= y[2 * i]; d += dat[0];			// [Sigma]xj^4
	}

	// Calculation of inverse matrix
	//----------------------------------------------
	dat[0] = num * b - a * a;						// 1) d[0] = P = nb-a^2		if P=0 then Error
	if (dat[0] != 0.0)
	{
		R_DAT = 1;								    // Not P=0
		dat[1] = a * b - num * c;					// 2) d[1] = S = ab-nc
		dat[2] = b * b;								// 3) Keep b^2 in memory (d[2] = b^2).	d[5] is defined.
		// 3) d[5] = Q = nP/((nd-b^2)P-S^2)
		// Operation for linear approximation.
		if (mode == 0)
		{					        				// < For linear approximation >
			dat[5] = g = 0.0;						//   Linear approximation is a specific case of qudratic approximation ([Sigma]xj^2*yj=0 and Q=0).
		}
		else
		{
			// RHM_HT 2013/11/27	Modified for dividing by Zero
			dat[5] = num * dat[0] / ((num * d - dat[2]) * dat[0] - dat[1] * dat[1]);
		}

		dat[3] = a * c - dat[2];					// 4) d[3] = R = ac-b^2, where d[2]=b^2 is used.
		c = dat[1] / dat[0];						// 6) c    = S/P = d[1]/d[0]
		d = dat[3] / dat[0];						// 7) d    = R/P = d[3]/d[0]
		dat[4] = c * dat[5];						// 9) d[4] = S/P*Q = c * d[5]
		dat[3] = num / dat[0] + c * dat[4];			// A) d[3] = num/d[0] + c * d[4]
		dat[2] = d * dat[5];						// B) d[2] = R/P*Q = d * d[5]
		dat[1] = -a / dat[0] + c * dat[2];			// C) d[1] = -a/d[0] + c * d[2]
		dat[0] = b / dat[0] + d * dat[2];			// D) d[0] =  b/d[0] + d * d[2]
	}
	else
	{
		R_DAT = 0;									// In the case of non-existence inverse matrix.

		// ==> RHM_HT 2013.03.24	for Enhanced error checking
		ret[1] = 0;
		ret[2] = 0;
		ret[3] = 0;
		ret[4] = 0;

		return;
		// <== RHM_HT 2013.03.24	for Enhanced error checking
	}

	// Calculate coefficient of aproximation
	//----------------------------------------------
	c_dat = dat[0] * e + dat[1] * f + dat[2] * g;
	b_dat = dat[1] * e + dat[3] * f + dat[4] * g;
	a_dat = dat[2] * e + dat[4] * f + dat[5] * g;

	//
	// x=-c/b     ( y    = bx+c           = 0) or
	// x=-b/(2a)  (dy/dx =d(ax^2+bx+c)/dx = 0)
	//----------------------------------------------
	if (mode == 0)
	{						        				// Linear approximation
		if (b_dat == 0.0)
		{
			Q_DAT = 0; R_DAT = 0;					// Error (divided by zero)
		}
		else
		{
			Q_DAT = -1.0 * c_dat / b_dat;
		}
	}
	else
	{												// Quadratic approximation
		if (a_dat == 0.0)
		{
			Q_DAT = 0; R_DAT = 0;					// Error (divided by zero)
		}
		else
		{
			Q_DAT = (-1.0 * b_dat / (2.0 * a_dat));
		}
	}

	// Calculation of (SSE,SST)
	//----------------------------------------------
	a = 0;
	b = 0;
	dat[1] = e / num;								// Average of y data
	for (i = 0; i < n; i++)
	{
		dat[0] = y[2 * i + 1];						// ( xn, yn ) = ( y[n][0], y[n][1] )
		dat[0] = (a_dat * y[2 * i] * y[2 * i])
				+ (b_dat * y[2 * i])
				+ c_dat							    // Difference from expected value 	( xn, yn ) = ( y[n][0], y[n][1] )
				- dat[0];							//
		a += (dat[0] * dat[0]);						// Square Sum of Errors (SSE)

		dat[0] = y[2 * i + 1] - dat[1];
		b += (dat[0] * dat[0]);						// Sum of Square for Total (SST)
	}

	// Calculation of r^2 value
	//----------------------------------------------
	if (R_DAT != 0)
	{												// if r^2 is zero, inverse matrix operation is error.
		R_DAT = 1 - a / b;		        			// R_DAT is percentage. ex) When r^2 is 98.1%, R_DAT is 98.1.
	}

	ret[1] = a_dat;
	ret[2] = b_dat;
	ret[3] = c_dat;
	ret[4] = R_DAT;

}


// /////////////////////////////////////////////////////////
// Get Test Chart Position
// ---------------------------------------------------------
// <Function>
//
//
// <Input>
//		char 		*path_file	Bitmap file name(include path name)
//		BMP_GET_POS	*param		Parameters
//
// <Output>
//		int						status
//
// <Description>
//
// =========================================================
int	GetTestChartPosition(char *path_file, const _BMP_GET_POS *param, _POS *results)
{
	int		sts = OIS_NO_ERROR;
	FILE	*orgBmp = NULL, *outBmp = NULL;										// RHM_HT 2013.03.27	Initialize to null
	BITMAPFILEHEADER	bmp_header;
	BITMAPINFOHEADER	bmp_infoheader;
 	OIS_UBYTE	dir = 0;
	OIS_LONG	width, height, i, k, m, index, read_length;
	OIS_LONG	c_width, c_height;
	OIS_UBYTE	*bm = NULL, *line = NULL, *clip = NULL, *bclip = NULL;			// RHM_HT 2013.03.27	Initialize to null
	char clip_file[256]="clip_";

    if (strcmp(param->direction, "hv") == 0 || strcmp(param->direction, "vh") == 0)
    {
        dir = 3;
        DEBUG_printf(("+-------------------------------------------+\n"));
        DEBUG_printf(("| Selected direction: Vertical & Horizontal |\n"));
        DEBUG_printf(("+-------------------------------------------+\n"));
    }
    else if (strcmp(param->direction, "h") == 0)
    {
        dir = 2;
        DEBUG_printf(("+--------------------------------+\n"));
        DEBUG_printf(("| Selected direction: Horizontal |\n"));
        DEBUG_printf(("+--------------------------------+\n"));
    }
    else if (strcmp(param->direction, "v") == 0)
    {
        dir = 1;
        DEBUG_printf(("+------------------------------+\n"));
        DEBUG_printf(("| Selected direction: Vertical |\n"));
        DEBUG_printf(("+------------------------------+\n"));
    }
    else
    {
        DEBUG_printf(("Prameter for direction is not collect!"));
        return OIS_INVALID_PARAMETERS;								// RHM_HT 2013/04/15	Set error number
    }

	// Open BMP file
	orgBmp = fopen(path_file, "rb");
	if( orgBmp == NULL ){
        DEBUG_printf(("%s not found!", path_file));
        return OIS_FILE_NOT_FOUND;									// RHM_HT 2013/04/15	Set error number
	}

	// Check file header
	fread(&bmp_header, sizeof(OIS_UBYTE), sizeof(BITMAPFILEHEADER), orgBmp);
// 	printf("bfType = %c%c\n", bmp_header.bfType & 0xff, bmp_header.bfType >> 8);
// 	printf("bfSize = %ld\n", bmp_header.bfSize);
// 	printf("bfOffBits = %ld\n", bmp_header.bfOffBits);

	// Check Bitmap file header
	fread(&bmp_infoheader, sizeof(OIS_UBYTE), sizeof(BITMAPINFOHEADER), orgBmp);
// 	printf("biSize = %ld\n", bmp_infoheader.biSize);
// 	printf("biWidth = %ld\n", bmp_infoheader.biWidth);
// 	printf("biHeight = %ld\n", bmp_infoheader.biHeight);
// 	printf("biPlanes = %d\n", bmp_infoheader.biPlanes);
// 	printf("biBitCount = %d\n", bmp_infoheader.biBitCount);
// 	printf("biCompression = %ld\n", bmp_infoheader.biCompression);
// 	printf("biSizeImage = %ld\n", bmp_infoheader.biSizeImage);
// 	printf("biXPelsPerMeter = %ld\n", bmp_infoheader.biXPelsPerMeter);
// 	printf("biYPelsPerMeter = %ld\n", bmp_infoheader.biYPelsPerMeter);
// 	printf("biClrUsed = %ld\n", bmp_infoheader.biClrUsed);
// 	printf("biClrImportant = %ld\n", bmp_infoheader.biClrImportant);

	// Memory Allocation for whole image
	width = bmp_infoheader.biWidth * 4;					// 32 bit mode
	height = bmp_infoheader.biHeight;
	if((bm = malloc(width * height)) == NULL){
		DEBUG_printf(("Memory allocation error for Bitmap. 1\n"));
		sts = OIS_MALLOC1_ERROR;									// RHM_HT 2013/04/15	Set error number
		goto GETTESTCHARTPOSITION_ERROR;
	}

	// Memory Allocation for 1 line
	if((line = malloc(width)) == NULL){
		DEBUG_printf(("Memory allocation error for Bitmap. 2\n"));
		sts = OIS_MALLOC2_ERROR;									// RHM_HT 2013/04/15	Set error number
		goto GETTESTCHARTPOSITION_ERROR;
	}

	// Memory Allocation for clip area
	if((clip = malloc(param->width * 4 * param->height)) == NULL){
		DEBUG_printf(("Memory allocation error for Bitmap. 3\n"));
		sts = OIS_MALLOC3_ERROR;									// RHM_HT 2013/04/15	Set error number
		goto GETTESTCHARTPOSITION_ERROR;
	}

	// Memory Allocation for binalized clip area
	if((bclip = malloc(param->width * 4 * param->height)) == NULL){
		DEBUG_printf(("Memory allocation error for Binalized Bitmap.\n"));
		sts = OIS_MALLOC4_ERROR;									// RHM_HT 2013/04/15	Set error number
		goto GETTESTCHARTPOSITION_ERROR;
	}

	// Copy whole bitmap to memory allocation area(1pixel = 4bytes)
	if( bmp_infoheader.biBitCount == 24 ){
		read_length = ((bmp_infoheader.biWidth * bmp_infoheader.biBitCount + 31) / 32) * 4;
	}
	else{
		read_length = width;
	}
//  	printf("read_lenght = %ld\n", read_length);
	for( i = height; i > 0; i--){
		if( fread(line, sizeof(OIS_UBYTE), read_length, orgBmp) == 0 ){
			DEBUG_printf(("Bitmap read error."));
			sts = OIS_BITMAP_READ_ERROR;							// RHM_HT 2013/04/15	Set error number
			goto GETTESTCHARTPOSITION_ERROR;						// RHM_HT 2013/04/15	Add "goto".
		}
		index = (i - 1) * width;
//  		printf("index = %ld\n", index);
		if( bmp_infoheader.biBitCount == 24 ){
			for( k = m = 0; k < width; k += 4, m += 3 ){	//
				bm[index + k + 0] = line[m + 0];			// B
				bm[index + k + 1] = line[m + 1];			// G
				bm[index + k + 2] = line[m + 2];			// R
				bm[index + k + 3] = 0;						// A
			}
		}
		else{
			for( k = m = 0; k < width; k += 4, m += 4 ){	//
				bm[index + k + 0] = line[m + 0];			// B
				bm[index + k + 1] = line[m + 1];			// G
				bm[index + k + 2] = line[m + 2];			// R
				bm[index + k + 3] = line[m + 3];			// A
			}
		}
	}

	// Clipping
	c_height = param->height;
	c_width = param->width;
	for( i = 0; i < c_height; i++ ){
		OIS_LONG	cofs = i * c_width * 4;
		OIS_LONG	bofs = ((param->y + i) * bmp_infoheader.biWidth + param->x) * 4;
		for( k = 0; k < (c_width * 4); k+= 4 ){
			clip[cofs + k + 0] = bm[bofs + k + 0];
			clip[cofs + k + 1] = bm[bofs + k + 1];
			clip[cofs + k + 2] = bm[bofs + k + 2];
			clip[cofs + k + 3] = bm[bofs + k + 3];
		}
//     		printf("x = %ld, y = %ld, clip = %ld, bmp = %ld\n",k, i, cofs, bofs);
	}

	// Save clip bitmap for DEBUG -- START --
// 	{
// 		// get file name from full path
// 		char *ptr;
// 		char temp[strlen(path_file)];
// 		char fname[512];
// 		strcpy( temp, path_file);
//
// 		ptr = strtok(temp, "\\");
// 		while(ptr != NULL){
// 			strcpy(fname, ptr);
// 			ptr = strtok(NULL, "\\");
// 		}
//
// 		strcat(clip_file, fname);
//
// 		outBmp = fopen(clip_file, "wb");
//
// 		bmp_header.bfSize = (param->width * 4 * param->height) + 54;
// 		bmp_header.bfOffBits = 54;
// 		fwrite(&bmp_header, sizeof(OIS_UBYTE), sizeof(BITMAPFILEHEADER), outBmp);
//
// 		bmp_infoheader.biSize = 40;
// 		bmp_infoheader.biWidth = param->width;
// 		bmp_infoheader.biHeight = param->height;
// 		bmp_infoheader.biBitCount = 32;
// 		bmp_infoheader.biSizeImage = 0;
// 		bmp_infoheader.biXPelsPerMeter = 0;
// 		bmp_infoheader.biYPelsPerMeter = 0;
// 		fwrite(&bmp_infoheader, sizeof(OIS_UBYTE), sizeof(BITMAPINFOHEADER), outBmp);
//
// 		fwrite(&clip[0], sizeof(OIS_UBYTE), param->width * 4 * param->height, outBmp);
//
// 		fclose(outBmp);
// 	}

	// Binalization
//  	printf("Binalize start.\n");
	// ==> RHM_HT 2013/04/15	for Enhanced error checking2
	sts = BinalizeBitmap(clip, (param->width * 4 * param->height), param->width, param->height, param->slice_level, param->filter, bclip);
	if( sts != OIS_NO_ERROR ){
		goto GETTESTCHARTPOSITION_ERROR;
	}
	// <== RHM_HT 2013/04/15	for Enhanced error checking2
//  	printf("Binalize done.\n");

	// Save clip bitmap for DEBUG -- START --
	// RHM_HT 2013.03.24	Delete comment
 	{
		// get file name from full path
		char *ptr;
		char fname[512];
		// ==> RHM_HT 2013.03.04	Modify for Cpp
		// char temp[strlen(path_file)];
		char *temp;
		if((temp = (char*)malloc(strlen(path_file))) == NULL){
			DEBUG_printf(("Memory allocation error for temp.\n"));
			sts = OIS_MALLOC5_ERROR;								// RHM_HT 2013/04/15	Set error number
			goto GETTESTCHARTPOSITION_ERROR;
		}
 		strcpy( temp, path_file);

 		ptr = strtok(temp, "\\");
 		while(ptr != NULL){
 			strcpy(fname, ptr);
 			ptr = strtok(NULL, "\\");
 		}

 		strcpy(clip_file, "bin_clip_");
 		strcat(clip_file, fname);

 		outBmp = fopen(clip_file, "wb");

 		bmp_header.bfSize = (param->width * 4 * param->height) + 54;
 		bmp_header.bfOffBits = 54;
 		fwrite(&bmp_header, sizeof(OIS_UBYTE), sizeof(BITMAPFILEHEADER), outBmp);

 		bmp_infoheader.biSize = 40;
 		bmp_infoheader.biWidth = param->width;
 		bmp_infoheader.biHeight = param->height;
 		bmp_infoheader.biBitCount = 32;
 		bmp_infoheader.biSizeImage = 0;
 		bmp_infoheader.biXPelsPerMeter = 0;
 		bmp_infoheader.biYPelsPerMeter = 0;
 		fwrite(&bmp_infoheader, sizeof(OIS_UBYTE), sizeof(BITMAPINFOHEADER), outBmp);

 		fwrite(&bclip[0], sizeof(OIS_UBYTE), param->width * 4 * param->height, outBmp);

 		fclose(outBmp);
 	}
	// Save clip bitmap for DEBUG -- END --

	{
		// ==> RHM_HT 2013.03.24	for Enhanced error checking
		sts = SearchCircle2(bclip, (param->width * 4 * param->height), param->width, param->height, results);
		if(sts != OIS_NO_ERROR){
			goto GETTESTCHARTPOSITION_ERROR;

		}
		// <== RHM_HT 2013.03.24	for Enhanced error checking

		for( i = 0; i < 4; i++ ){
			OIS_ULONG	p = ((OIS_ULONG)results[i].y * param->width + (OIS_ULONG)results[i].x) * 4;
// 			printf("(%f, %f), %ld\n", results[i].x, results[i].y, p);
			// ==> RHM_HT 2013.03.24	for Enhanced error checking
			if(p >= (param->width * 4 * param->height)){
				sts = OIS_CHART_ARRAY_OVER;							// RHM_HT 2013/04/15	Set error number
				goto GETTESTCHARTPOSITION_ERROR;
			}
			// <== RHM_HT 2013.03.24	for Enhanced error checking
			bclip[p + 0] = 0;
			bclip[p + 1] = 0;
			bclip[p + 2] = 255;

			bclip[p + 0 + 4] = 0;
			bclip[p + 1 + 4] = 0;
			bclip[p + 2 + 4] = 255;

			bclip[p + (param->width * 4) + 0] = 0;
			bclip[p + (param->width * 4) + 1] = 0;
			bclip[p + (param->width * 4) + 2] = 255;

			bclip[p + (param->width * 4) + 0 + 4] = 0;
			bclip[p + (param->width * 4) + 1 + 4] = 0;
			bclip[p + (param->width * 4) + 2 + 4] = 255;
		}
	}

	// Save clip bitmap for DEBUG -- START --
	// ==> RHM_HT 2013.03.24	Commented out
//	{
//		// get file name from full path
//		char *ptr;
//		char fname[512];
//		// ==> RHM_HT 2013.03.04	Modify for Cpp
//		// char temp[strlen(path_file)];
//		char *temp;
//		if((temp = (char*)malloc(strlen(path_file))) == NULL){
//			DEBUG_printf(("Memory allocation error for temp.\n"));
//			exit(1);
//		}
//		// <== RHM_HT 2013.03.04
//
//		strcpy( temp, path_file);
//
//		ptr = strtok(temp, "\\");
//		while(ptr != NULL){
//// 			printf("filename is %s\n", ptr);
//			strcpy(fname, ptr);
//			ptr = strtok(NULL, "\\");
//		}
//
//		strcpy(clip_file, "bin_clip_approx_");
//		strcat(clip_file, fname);
//
//		outBmp = fopen(clip_file, "wb");
//
//		bmp_header.bfSize = (param->width * 4 * param->height) + 54;
//		bmp_header.bfOffBits = 54;
//		fwrite(&bmp_header, sizeof(OIS_UBYTE), sizeof(BITMAPFILEHEADER), outBmp);
//
//		bmp_infoheader.biSize = 40;
//		bmp_infoheader.biWidth = param->width;
//		bmp_infoheader.biHeight = param->height;
//		bmp_infoheader.biBitCount = 32;
//		bmp_infoheader.biSizeImage = 0;
//		bmp_infoheader.biXPelsPerMeter = 0;
//		bmp_infoheader.biYPelsPerMeter = 0;
//		fwrite(&bmp_infoheader, sizeof(OIS_UBYTE), sizeof(BITMAPINFOHEADER), outBmp);
//
//		fwrite(&bclip[0], sizeof(OIS_UBYTE), param->width * 4 * param->height, outBmp);
//
//		fclose(outBmp);
//	}
	// <== RHM_HT 2013.03.24	Commented out
	// Save clip bitmap for DEBUG -- END --




GETTESTCHARTPOSITION_ERROR:
	free(clip);
	free(bclip);
	free(line);
	free(bm);
	if (orgBmp != NULL )	fclose(orgBmp);		// RHM_HT 2013.03.27	Modify
    return sts;
}


// /////////////////////////////////////////////////////////
// Binalization of Bitmap Image
// ---------------------------------------------------------
// <Function>
//
//
// <Input>
//		const OIS_UBYTE *ba		Pointer of source image bitmap array
//		OIS_LONG 		len		Length of source image bitmap array
//		OIS_WORD 		width	Width of source image
//		OIS_WORD 		height	Height of source image
//		OIS_UBYTE 		slevel	Slice level for binalization
//		OIS_UBYTE 		fenable	1 = Enable median filter
//		OIS_UBYTE 		*baaray	Pointer of distination image bitmap array
//
// <Output>
//		none
//
// <Description>
//
// =========================================================
int BinalizeBitmap(const OIS_UBYTE *ba, OIS_LONG len, OIS_WORD width, OIS_WORD height, OIS_UBYTE slevel, OIS_UBYTE fenable, OIS_UBYTE *barray)	// RHM_HT 2013/04/15	Change "typedef" of return value
{
	int		sts = OIS_NO_ERROR;			// RHM_HT 2013/04/15	Set error number
	OIS_UWORD	*gs = NULL;				// RHM_HT 2013.03.27	Initialize to null
    OIS_UWORD	*mf = NULL;				// RHM_HT 2013.03.27	Initialize to null
	int		sts = OIS_NO_ERROR;			// RHM_HT 2013/04/15	Set error number

    if (slevel >= 0)
    {
        OIS_UBYTE LUT[256 * 3];
		OIS_LONG	i, k;

// 		printf("length = %ld, width = %d, height = %d\n", len, width, height);

        // Create table for binalization
		memset(LUT, 0, sizeof(LUT));				// Zero fill
        for (i = slevel * 3; i < sizeof(LUT); i++)
        {
            LUT[i] = 0xFF;
        }

        // Median filter
        if (fenable == 1)
        {
// 			OIS_UWORD	gs[height][width];
//             OIS_UWORD	mf[width * height];
            OIS_UWORD	map[9];

			// Memory Allocation
			if((gs = malloc(width * height * sizeof(OIS_UWORD))) == NULL){
				DEBUG_printf(("Memory allocation error for Filterling."));
				sts = OIS_MALLOC6_ERROR;									// RHM_HT 2013/04/15	Set error number
				goto BINALIZEBITMAP_ERROR;
			}

			// Memory Allocation
			if((mf = malloc(width * height * sizeof(OIS_UWORD))) == NULL){
				DEBUG_printf(("Memory allocation error for Filterling."));
				sts = OIS_MALLOC7_ERROR;									// RHM_HT 2013/04/15	Set error number
				goto BINALIZEBITMAP_ERROR;
			}

            // 1. Convert to Gray scale
// 			printf("Convert to gray scale.\n");
            for (i = 0; i < height; i++)
            {
                OIS_LONG index = (i * width) * 4;

                for (k = 0; k < width; k++, index += 4)
                {
                    // BGRA
                    gs[i * width + k] = (OIS_UWORD)(ba[index + 0] + ba[index + 1] + ba[index + 2]);
//                    	printf("%02X,", gs[i * width + k]/3);
                }
//                	printf("\n");
            }
            // 2. Filterling
// 			printf("Filterling.\n");
            {
				OIS_UWORD	*ptr = &map[0], *ptr2 = &mf[0];
                OIS_UWORD 	temp;
                OIS_UWORD	*mptr;
                OIS_UWORD	*mfptr;
				OIS_UWORD	m, n;
                for (i = 1; i < height - 1; i++)
                {

                    mfptr = ptr2 + i * width + 1;

                    for (k = 1; k < width - 1; k++)
                    {
                        map[0] = gs[(i - 1) * width + (k - 1)];
                        map[1] = gs[(i - 1) * width + (k + 0)];
                        map[2] = gs[(i - 1) * width + (k + 1)];
                        map[3] = gs[(i + 0) * width + (k - 1)];
                        map[4] = gs[(i + 0) * width + (k + 0)];
                        map[5] = gs[(i + 0) * width + (k + 1)];
                        map[6] = gs[(i + 1) * width + (k - 1)];
                        map[7] = gs[(i + 1) * width + (k + 0)];
                        map[8] = gs[(i + 1) * width + (k + 1)];
                        // Mininmall bubble soort to ask for median value.
                        for (m = 0; m < 5; m++)
                        {
                            mptr = ptr;
                            for (n = 0; n < 8; n++)
                            {
                                if (mptr[0] > mptr[1])
                                {
                                    temp = mptr[1];
                                    mptr[1] = mptr[0];
                                    mptr[0] = temp;
                                }
                                mptr++;
                            }
                        }

                        *mfptr = map[4];
                        //Debug code
                        //Array.Sort(map);
                        //if (map[4] != *mfptr)
                        //{
                        //    Console.WriteLine("Different!! " + map[4] +", "+ *mfptr);
                        //}
                        mfptr++;
//                      	printf("%02X,", map[4]/3);
                    }
//                     	printf("\n");
                }
            }

			// It is considered that first line (row or colum) is the same as second line and
			// last line is the same as the front of that line.
            for (k = 1; k < width - 1; k++)
            {
                mf[k] = mf[width + k];
                mf[(height - 1) * width + k] = mf[(height - 2) * width + k];
            }
            for (i = 0; i < height; i++)
            {
                mf[i * width] = mf[i * width + 1];
                mf[i * width + (width - 1)] = mf[i * width + (width - 2)];
            }

            // Binalization
            for (i = 0; i < (width * height); i++)
            {
				barray[i * 4 + 2] = barray[i * 4 + 1] = barray[i * 4 + 0] = LUT[mf[i]];     // RGB
                barray[i * 4 + 3] = 0xff;   												// alfa
//                	printf("%02X,", LUT[mf[i]]);
// 				if( ((i + 1 ) % width) == 0 ){
//                    	printf("\n");
// 				}
            }
        }
        else
        {
            OIS_LONG	pixsize = width * height * 4;
			OIS_UWORD	avg;

            // Binalization
            for (i = 0; i < pixsize; i += 4)
            {
                avg = (OIS_UWORD)(ba[i + 0] + ba[i + 1] + ba[i + 2]);
                barray[i + 2] = barray[i + 1] = barray[i + 0] = LUT[avg];      				// RGB
                barray[i + 3] = 0xff;   													// alfa
            }
        }
    }
	else{
		memcpy(barray, ba, len);
	}

BINALIZEBITMAP_ERROR:
	free(gs);
	free(mf);
	return	sts;													// RHM_HT 2013/04/15	Add
}

int posCompareByX(const void *a, const void *b)
{
	if( ((_POS *)a)->x > ((_POS *)b)->x ){
		return 1;
	}
	else if( ((_POS *)a)->x <  ((_POS *)b)->x ){
		return -1;
	}

	return 0;
}


int posCompareByY(const void *a, const void *b)
{
	if( ((_POS *)a)->y > ((_POS *)b)->y ){
		return 1;
	}
	else if( ((_POS *)a)->y <  ((_POS *)b)->y ){
		return -1;
	}

	return 0;
}


// /////////////////////////////////////////////////////////
// Get Inverse Matrix
// ---------------------------------------------------------
// <Function>
//		This fuction asks for inverse matrix.
//
// <Input>
//		double *Mat		Original matrix (source, 1D array)
//		double *Inv 	Inverse matrix	(distination, 1D array)
//		int n 			matrix size (n x n)
//
// <Output>
//		Status
//
// <Description>
//		Note: Contents of *Mat matrix are replaced identity matrix.
//
// =========================================================
int MatrixInverse(double *Mat, double *Inv, int n)
{
    int i, j, k;
    double temp;

    //Initialize by identity matrix
    for (j = 0; j < n; j++)
    {
        for (i = 0; i < n; i++)
        {
            if (i == j)
            {
                Inv[i + j * n] = 1;
            }
            else
            {
                Inv[i + j * n] = 0;
            }
        }
    }

    for (k = 0; k < n; k++)
    {
        //Set 1 to diagonal elements
        temp = Mat[k + k * n];
        if (temp == 0) return OIS_MATRIX_INV_ERROR;    				// RHM_HT 2013/04/15	Set error number
        for (i = 0; i < n; i++)
        {
            Mat[i + k * n] /= temp;
            Inv[i + k * n] /= temp;
        }

        for (j = 0; j < n; j++)
        {
            if (j != k)
            {
                temp = Mat[k + j * n] / Mat[k + k * n];
                for (i = 0; i < n; i++)
                {
                    Mat[i + j * n] -= Mat[i + k * n] * temp;
                    Inv[i + j * n] -= Inv[i + k * n] * temp;
                }
            }
        }
    }
    //End of function
    return OIS_NO_ERROR;

}

// /////////////////////////////////////////////////////////
// Get Inverse Matrix
// ---------------------------------------------------------
// <Function>
//		This fuction asks for the center of circle.
//
// <Input>
//		_POS *plist						Position data of line of circle.
//		OIS_UWORD plist_count 			Number of above position data.
//		_APPROXRESULT *approxResult		Result of circlular approximation
//
// <Output>
//		Status
//
// <Description>
//
// =========================================================
void CircularApproximation(_POS *plist, OIS_UWORD plist_count, _APPROXRESULT *approxResult)
{
    double X2, XY, X, Y2, Y;
    double X3PXY2, X2YPY3, X2PY2;
    double A, B, C;
    double cnt = plist_count;

	double mat[9];
    double inv[9];

	int i;

    X2 = 0;
    XY = 0;
    X = 0;
    Y2 = 0;
    Y = 0;
    X3PXY2 = 0;
    X2YPY3 = 0;
    X2PY2 = 0;

    // Preparation of circular approximation
    for (i = 0; i < plist_count; i++)
    {
        double x2_temp, y2_temp;

        x2_temp = (plist[i].x * plist[i].x);
        y2_temp = (plist[i].y * plist[i].y);

        X2 += (x2_temp / cnt);
        XY += ((plist[i].x * plist[i].y) / cnt);
        X += (plist[i].x / cnt);
        Y2 += (y2_temp / cnt);
        Y += (plist[i].y / cnt);

        X3PXY2 += ((plist[i].x * (x2_temp + y2_temp)) / cnt);
        X2YPY3 += ((plist[i].y * (x2_temp + y2_temp)) / cnt);
    }
    X2PY2 = X2 + Y2;

    mat[0] = X2; mat[1] = XY; mat[2] = X;
    mat[3] = XY; mat[4] = Y2; mat[5] = Y;
    mat[6] = X; mat[7] = Y; mat[8] = 1;

    // Get inverse matrix
    MatrixInverse(mat, inv, 3);

	// Ask for center of circle (x, y) and radius by method of least square.
    A = inv[0] * (-X3PXY2) + inv[1] * (-X2YPY3) + inv[2] * (-X2PY2);
    B = inv[3] * (-X3PXY2) + inv[4] * (-X2YPY3) + inv[5] * (-X2PY2);
    C = inv[6] * (-X3PXY2) + inv[7] * (-X2YPY3) + inv[8] * (-X2PY2);

    approxResult->a = (A / (-2));
    approxResult->b = (B / (-2));
    approxResult->r = sqrt(approxResult->a * approxResult->a + approxResult->b * approxResult->b - C);
}


// /////////////////////////////////////////////////////////
// Ask for the center of each circles in OIS Test Chart.
// ---------------------------------------------------------
// <Function>
//		This fuction asks for the center of each circles (4 circles)
// 		from the binalized OIS Test chart image (32 bit bitmap array).
//
// <Input>
//		OIS_UBYTE *ba					Binalized bitmap 1d array (B, G, R, A, B, G,...)
//		OIS_LONG len 					Length of bitmap array.
//		OIS_WORD width					Bitmap width
//		OIS_WORD height					Bitmap height
// 		_POS *pos						Pointer for results (4 array)
//
// <Output>
//		Status
//
// <Description>
//
// =========================================================
int SearchCircle2(OIS_UBYTE *ba, OIS_LONG len, OIS_WORD width, OIS_WORD height, _POS *pos)
{
    // Initialize
	int		sts = OIS_NO_ERROR;										// RHM_HT 2013/04/15	Add
	int		i, k;													// RHM_HT 2013.03.23	move to here
	int		pidx = 0;					// Max Index of temp_pos	   RHM_HT 2013.03.23	move to here
	// ==> RHM_HT 2013.03.04	Modify for Cpp
	// _POS	temp_pos[height * 10];		// big enough
    _POS	*temp_pos = NULL;										// RHM_HT 2013/04/15	Initialize to NULL.
	if((temp_pos = (_POS*)malloc(sizeof(_POS) * height * 10)) == NULL){
		DEBUG_printf(("Memory allocation error for _POS. 1\n"));
		return OIS_MALLOC8_ERROR;									// RHM_HT 2013/04/15	Set error number
	}
	// <== RHM_HT 2013.03.04

	// ==> RHM_HT 2013.03.24	Add intialize
	for( i = 0; i < 4; i++){
		pos[i].x = 0;
		pos[i].y = 0;
	}
	// <== RHM_HT 2013.03.24	Add intialize

	// Edge detection (Black <-> White)
	{
		int		index;
	    int 	nextLineOfs = width * 4;
		int		pidxLimit = height * 10;	// RHM_HT 2013.03.24	for Enhanced error checking

	    for (i = 1; i < height - 1; i++)
	    {
	        index = (i * width) * 4 + 4;

	        for (k = 1; k < width - 1; k++, index += 4)
	        {
	            if( ba[index] == 0 ){
					continue;
				}

				// ==> RHM_HT 2013.03.24	for Enhanced error checking
				if( pidx >= pidxLimit){
					free(temp_pos);
					return	OIS_SC2_XLIMIT_OVER;					// RHM_HT 2013/04/15	Set error number
				}
				// <== RHM_HT 2013.03.24	for Enhanced error checking

				if (ba[index - 4] != ba[index])
	            {
	                temp_pos[pidx].x = k;
			        temp_pos[pidx].y = i;
					pidx++;
	            }
	            else if (ba[index + 4] != ba[index])
	            {
	                temp_pos[pidx].x = k;
			        temp_pos[pidx].y = i;
					pidx++;
				}
	            else
	            {
	                if (ba[index - nextLineOfs] != ba[index])
	                {
		                temp_pos[pidx].x = k;
				        temp_pos[pidx].y = i;
						pidx++;
	                }
	                else if (ba[index + nextLineOfs] != ba[index])
	                {
		                temp_pos[pidx].x = k;
				        temp_pos[pidx].y = i;
						pidx++;
	                }
	            }
	        }
	    }
	}


    // Groped for 4 circles
    // 1. First, sort by x to 3 groups and grouped by long x distance.
	// 2. Next, sort the second group by y, and separate by long y distance.
// 	printf("Before search...\n");
// 	for( i = 0; i < sizeof(temp_pos)/sizeof(_pos); i++ ){
// 		printf("(%f, %f)\n", temp_pos[i].x, temp_pos[i].y);
// 	}
	qsort(temp_pos, pidx, sizeof(_POS), posCompareByX);						// sort by x
// 	printf("After search...\n");
// 	for( i = 0; i < sizeof(temp_pos)/sizeof(_POS); i++ ){
// 		printf("(%f, %f)\n", temp_pos[i].x, temp_pos[i].y);
// 	}

// 	printf("size = %d, pidx = %d\n", sizeof(temp_pos)/sizeof(_POS), pidx);
	for(i = 0; i < pidx; i++){
		ba[(OIS_ULONG)temp_pos[i].x * 4 + (OIS_ULONG)temp_pos[i].y * width * 4] = 128;
//  		printf("(%f, %f)\n", temp_pos[i].x, temp_pos[i].y);
	}

	{
	    int 	x1 = 0, x2 = 0;
	    float 	max1 = 0, max2 = 0;
		float 	dist;
        int g1_size, g2_size, g2_temp_size, g3_size, g4_size;

	    for (i = 1; i < pidx; i++)
	    {
	        dist = temp_pos[i].x - temp_pos[i - 1].x;
	        if (dist > max1)
	        {
	            // Update the scond largest value.
	            max2 = max1;
	            x2 = x1;

	            max1 = dist;
	            x1 = i;
	        }
	        else
	        {
	            if (dist > max2)
	            {
	                max2 = dist;
	                x2 = i;
	            }
	        }
	    }

	    if (x1 > x2)
	    {
	        // x1 has to be smaller than x2.
	        int xTemp = x1;

	        x1 = x2;
	        x2 = xTemp;

	    }

	    // Grouping 1
		{
			// ==> RHM_HT 2013.03.04	Modify for Cpp
			// _POS	g1[x1], g2_temp[x2-x1], g4[pidx-x2];
            _POS	*g1 = NULL; 											// RHM_HT 2013/04/15	Initilalize to NULL.
			_POS	*g2_temp = NULL; 										// RHM_HT 2013/04/15	Initilalize to NULL.
			_POS	*g4 = NULL;												// RHM_HT 2013/04/15	Initilalize to NULL.
            _POS	*g2 = NULL;												// RHM_HT 2013/04/15	Initilalize to NULL.
			_POS	*g3 = NULL;												// RHM_HT 2013/04/15	Initilalize to NULL.

            g1_size = sizeof(_POS)*(x1);
			g2_temp_size = sizeof(_POS)*(x2-x1);
            g4_size = sizeof(_POS)*(pidx-x2);
            if((g1 = (_POS*)malloc(g1_size)) == NULL){
		        DEBUG_printf(("Memory allocation error for _POS. 2\n"));
				sts = OIS_MALLOC9_ERROR;									// RHM_HT 2013/04/15	Set error number
				goto SEARCHCIRCLE2_ERROR;									// RHM_HT 2013/04/15	Change "exit" to "goto"
	        }
            if((g2_temp = (_POS*)malloc(g2_temp_size)) == NULL){
		        DEBUG_printf(("Memory allocation error for _POS. 3\n"));
				sts = OIS_MALLOC10_ERROR;									// RHM_HT 2013/04/15	Set error number
				goto SEARCHCIRCLE2_ERROR;									// RHM_HT 2013/04/15	Change "exit" to "goto"
	        }
            if((g4 = (_POS*)malloc(g4_size)) == NULL){
		        DEBUG_printf(("Memory allocation error for _POS. 4\n"));
				sts = OIS_MALLOC11_ERROR;									// RHM_HT 2013/04/15	Set error number
				goto SEARCHCIRCLE2_ERROR;									// RHM_HT 2013/04/15	Change "exit" to "goto"
	        }

			// memcpy(&g1[0], &temp_pos[0], sizeof(_POS)*x1);
			// memcpy(&g2_temp[0], &temp_pos[x1], sizeof(_POS)*(x2 - x1));
			// memcpy(&g4[0], &temp_pos[x2], sizeof(_POS)*(pidx - x2));
			memcpy(&g1[0], &temp_pos[0], g1_size);								// RHM_HT 2013.03.04	Modify
			memcpy(&g2_temp[0], &temp_pos[x1], g2_temp_size);					// RHM_HT 2013.03.04	Modify
			memcpy(&g4[0], &temp_pos[x2], g4_size);								// RHM_HT 2013.03.04	Modify
			// <== RHM_HT 2013.03.04

		    x1 = 0; max1 = 0;
			// ==> RHM_HT 2013.03.04	Modify for Cpp
			// qsort(g2_temp, sizeof(g2_temp)/sizeof(_POS), sizeof(_POS), posCompareByY);	// Sort by y
			// for (i = 1; i < sizeof(g2_temp)/sizeof(_POS); i++)
			qsort(g2_temp, (g2_temp_size)/sizeof(_POS), sizeof(_POS), posCompareByY);	// Sort by y
		    for (i = 1; i < (g2_temp_size)/sizeof(_POS); i++)
			// <== RHM_HT 2013.03.04
		    {
		        dist = g2_temp[i].y - g2_temp[i - 1].y;
		        if (dist > max1)
		        {
		            max1 = dist;
		            x1 = i;
		        }
		    }

		    // Grouping 2
			{
				// ==> RHM_HT 2013.03.04	Modify for Cpp
				// _POS	g2[x1], g3[sizeof(g2_temp)/sizeof(_POS) - x1];
				_APPROXRESULT	results;

                g2_size = sizeof(_POS)*(x1);
                g3_size = sizeof(_POS)*(g2_temp_size/sizeof(_POS) - x1);
                if((g2 = (_POS*)malloc(g2_size)) == NULL){
		            DEBUG_printf(("Memory allocation error for _POS. 5\n"));
					sts = OIS_MALLOC12_ERROR;										// RHM_HT 2013/04/15	Set error number
					goto SEARCHCIRCLE2_ERROR;										// RHM_HT 2013/04/15	Change "exit" to "goto"
	            }
                if((g3 = (_POS*)malloc(g3_size)) == NULL){
		            DEBUG_printf(("Memory allocation error for _POS. 6\n"));
					sts = OIS_MALLOC13_ERROR;										// RHM_HT 2013/04/15	Set error number
					goto SEARCHCIRCLE2_ERROR;										// RHM_HT 2013/04/15	Change "exit" to "goto"
	            }
				// memcpy(&g2[0], &g2_temp[0], sizeof(_POS)*x1);
				// memcpy(&g3[0], &g2_temp[x1], sizeof(g3));
				memcpy(&g2[0], &g2_temp[0], g2_size);								// RHM_HT 2013.03.04	Modify
				memcpy(&g3[0], &g2_temp[x1], g3_size);								// RHM_HT 2013.03.04	Modify
				// <== RHM_HT 2013.03.04

// 				for(i = 0; i < sizeof(g1)/sizeof(_POS); i++){
// 					ba[(OIS_ULONG)g1[i].x * 4 + (OIS_ULONG)g1[i].y * width * 4] = 128;
// 				}
// 				for(i = 0; i < sizeof(g2)/sizeof(_POS); i++){
// 					ba[(OIS_ULONG)g2[i].x * 4 + (OIS_ULONG)g2[i].y * width * 4] = 128;
// 				}
// 				for(i = 0; i < sizeof(g3)/sizeof(_POS); i++){
// 					ba[(OIS_ULONG)g3[i].x * 4 + (OIS_ULONG)g3[i].y * width * 4] = 128;
// 				}
// 				for(i = 0; i < sizeof(g4)/sizeof(_POS); i++){
// 					ba[(OIS_ULONG)g4[i].x * 4 + (OIS_ULONG)g4[i].y * width * 4] = 128;
// 				}

				// Approximation
				// ==> RHM_HT 2013.03.04	Modify for Cpp
		        //CircularApproximation(&g1[0], sizeof(g1)/sizeof(_POS), &results);
                CircularApproximation(&g1[0], (g1_size)/sizeof(_POS), &results);
				pos[0].x = results.a;
				pos[0].y = results.b;
		        //CircularApproximation(&g2[0], sizeof(g2)/sizeof(_POS), &results);
                CircularApproximation(&g2[0], (g2_size)/sizeof(_POS), &results);
				pos[1].x = results.a;
				pos[1].y = results.b;
		        //CircularApproximation(&g3[0], sizeof(g3)/sizeof(_POS), &results);
                CircularApproximation(&g3[0], (g3_size)/sizeof(_POS), &results);
				pos[2].x = results.a;
				pos[2].y = results.b;
		        //CircularApproximation(&g4[0], sizeof(g4)/sizeof(_POS), &results);
                CircularApproximation(&g4[0], (g4_size)/sizeof(_POS), &results);
				pos[3].x = results.a;
				pos[3].y = results.b;

SEARCHCIRCLE2_ERROR:												// RHM_HT 2013/04/15	Set label
				free(temp_pos);
				free(g1);
				free(g2);
				free(g2_temp);
				free(g3);
				free(g4);
				// <== RHM_HT 2013.03.04
			}
		}
	}
    return sts;														// RHM_HT 2013/04/15	Change to "sts".
}


// ==> RHM_HT 2013/11/25	Add new function for cross-talk cancelaration.
// /////////////////////////////////////////////////////////
// Ask for the center of each circles in OIS Test Chart.
// ---------------------------------------------------------
// <Function>
//		This fuction asks for the cross-talk compensation
// 		from (x, y) data of center of circle
//
// <Input>
//		OIS_WORD	inpCount	Total number of array pointed next argument.
//		double		*x			Input pointer of x-position data (dbl_pos_x in func_HALL_SENSE_ADJUST).
//		double		*y			Input pointer of y-position data (dbl_pos_y in func_HALL_SENSE_ADJUST).
//		double		*val		Output pointer of cross-talk compensation value.
//
// <Output>
//		Status
//
// <Description>
//
// =========================================================
int    CompensateCrossTalk(OIS_WORD inpCount, double *x, double *y, double *val)
{
    OIS_UWORD	i;
	double		ret[5] = {0,0,0,0,0};								// double, note: [0] is reserved.

	double	*ct_pos;
	if((ct_pos = (double*)malloc(sizeof(double) * inpCount)) == NULL){
		DEBUG_printf(("Memory allocation error for ct_pos.\n"));
		return OIS_MALLOC14_ERROR;
	}
	// -----------------------------------------------------------------------------------------------------------------
    // We get the cross-talk compensation between X and Y.
    // Avoiding the error of the method ofleast square, we have to rotate the xy plots 45 degrees.
    // -----------------------------------------------------------------------------------------------------------------
    DEBUG_printf(("Original\nx,y\n"));
    for (i = 0; i < inpCount; i+=2)
    {
        DEBUG_printf(("%.4f,%.4f\n", x[i], y[i]));
    }
    DEBUG_printf(("45deg rotated\nx',y'\n"));
    for (i = 0; i < inpCount; i+=2)
    {
        ct_pos[i] = cos(M_PI / 4) * x[i] + sin(M_PI / 4) * y[i];
        ct_pos[i + 1] = -sin(M_PI / 4) * x[i] + cos(M_PI / 4) * y[i];
        DEBUG_printf(("%.4f,%.4f\n", ct_pos[i], ct_pos[i + 1]));
    }

	Approx(0, inpCount, ct_pos, ret);
    DEBUG_printf(("a = %.4f, b = %.4f, R^2 = %.4f\n", ret[2], ret[3], ret[4]));

    if (ret[2] < 0)
    {
        // we use ret[0] because it is reserved (non-use) variable.
		ret[0] = tan(atan(-1.0) - atan(ret[2]));
    }
    else
    {
        ret[0] = tan(atan(ret[2]) - atan(1.0));
    }
    DEBUG_printf(("Correction value\n"));
    DEBUG_printf(("%.4f\n", ret[0]));

	*val = ret[0];

	free(ct_pos);
	return OIS_NO_ERROR;
}
#endif

// <== RHM_HT 2013/11/25	Add new function for cross-talk cancelaration.

