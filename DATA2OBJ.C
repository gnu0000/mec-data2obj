/*
 *
 * data2obj.c
 * Friday, 8/20/1993.
 *
 * Craig Fitzgerald
 *
 * This program creates 16 bit Microsoft/Intel compatible OBJ files
 * These obj files are also readable by watcoms 32 bit linker
 * (and probably others).  There are several caveats and gotchas
 * associated with obj files so make sure you know what you are
 * doing before modifying this code.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GnuType.h>
#include <GnuMem.h>
#include <GnuArg.h>
#include <GnuMisc.h>
#include <GnuFile.h>
#include <GnuStr.h>
#include <GnuCfg.h>

#define MAXLESIZE    1000
#define LINESIZE     512
#define MAXPPSZLINES 8096


/*
 * LNAMES info
 */
typedef struct
   {
   PSZ    pszName;         // name string
   USHORT uIndex;          // 1 based name index;
   } LNAME;
typedef LNAME *PLNAME;


/*
 * SEGDEF info
 *
 */
typedef struct
   {
   PSZ    pszSeg;      // the name of the segment 
   PSZ    pszCls;      // the name of the segment class
   USHORT uSegNameIdx; // index of segment name
   USHORT uClsNameIdx; // index of class   name
   USHORT uSegDefIdx;  // 1 based index of this segdef record
   USHORT uFlags;      // segment type flags.
   USHORT uSegLen;     // length of this segment
   ULONG  ulSegLenPos; // position of uSegLen in output obj file for fixup.
   } SEGDEF;
typedef SEGDEF *PSEGDEF;


/*
 * Entry in cfg file
 *
 */
typedef struct
   {                   
   FILE   *fp;         // file containing data
   ULONG  ulDataPos;   // position of source data in input file
                       
   PSZ    pszVar;      // variable name
   PSZ    pszType;     // variable type
   PSZ    pszSeg;      // segment name
   USHORT uSegDefIdx;  // SEGDEF index
   USHORT uDataLen;    // data length for reporting
   ULONG  ulOffsetPos; // position of offset in output file
   USHORT uSegOffset;  // offset into dest file segment for data dest
   USHORT uSegSOffset; // offset into dest file segment for PPSZ strings
   USHORT uDataType;   // 0=psz, 1=ppsz, 2=ushort, 3=ulong, 4=bin
   } ENTRY;
typedef ENTRY *PENTRY;


/*
 * Mr. everything structure whose sole
 * purpose in life is to minimize the number of
 * parameters i have to keep track of
 */
typedef struct
   {
   FILE    *fpIn;
   FILE    *fpOut;
   PSZ     pszInFile;
   PSZ     pszOutFile;
   PSZ     pszHFile;
   PLNAME  plname;
   PSEGDEF pseg;
   PENTRY  pentry;
   BOOL    bInLine;
   } O;
typedef O *PO;


/*
 * BOOLEAN options set at the
 * command line.
 *
 */
BOOL bDEBUG;          // Turns on Debug Info
BOOL bVERBOSE;        // Displays info about vars
BOOL bHEADER;         // Creates a Header File
BOOL bSIZES;          // Adds #defines to the created header file
BOOL bINLINE;         // Use a src file rather than a dat file

USHORT uALIGNMENT;    // Alignment size (1,2,4,16)
USHORT uNEWSEG;       // Index of first seg to create

char  szBuff    [MAXLESIZE+1];

ULONG ulOffsets [MAXPPSZLINES];         // for PPSZ lines

USHORT uCLASSNAMEIDX;



/*************************************************************************/
/*                                                                       */
/*  Utility Stuff                                                        */
/*                                                                       */
/*************************************************************************/

USHORT _cdecl MyError (PO po, PSZ psz, ...)
   {
   printf ("Error: ");
   vprintf (psz, (PSZ) (&psz + 1));
   printf ("\n");
   if (po->fpOut)
      fclose (po->fpOut);
   unlink (po->pszOutFile);
   exit (1);
   return 0;
   }



static void xwrite (USHORT uNum, USHORT uLen, FILE *fp)
   {
   fwrite ((PSZ) (PVOID) &uNum, 1, uLen, fp);
   }


static void dwrite (PVOID pv, USHORT uLen, FILE *fp)
   {
   fwrite ((PSZ) pv, 1, uLen, fp);
   }


static USHORT hexcharval (USHORT c)
   {
   c = toupper (c);
   if (!strchr ("0123456789ABCDEF", c))
      return 0;
   return (c - '0' - 7 * (c > '9'));
   }


static USHORT hexval (PSZ psz)
   {
   if (!psz || !*psz || !psz[1])
      return 0;
   return 16 * hexcharval (psz[0]) + hexcharval (psz[1]);
   }



PSZ GetFileName (PSZ pszDest, PSZ pszBase, PSZ pszOrg, PSZ pszExt)
   {
   PSZ p1, p2;

   if (pszBase)
      {
      strcpy (pszDest, pszBase);
      if (!strchr (pszDest, '.') && pszExt)
         strcat (pszDest, pszExt);
      return pszDest;
      }
   strcpy (pszDest, pszOrg);
   p2 = ((p1 = strrchr (pszDest, '\\') ? p1 + 1: pszDest));
   if (p1 = strrchr (p2, '.'))
      *p1 = '\0';
   strcat (p2, pszExt);
   return pszDest;
   }



/*************************************************************************/
/*                                                                       */
/*  Self contained field Stuff                                           */
/*                                                                       */
/*************************************************************************/


void WriteMODEND (PO po)
   {
   fwrite  ("\x8A\x02\x00\x00\x74", 1, 5, po->fpOut);
   }


void WriteCOMENT (PO po)
   {
   fwrite  ("\x88\x04\x00\x00\xA2\x01\xD1", 1, 7, po->fpOut);
   }


void WriteTHEADR (PO po)
   {
   USHORT uLen;

   uLen = strlen (po->pszInFile);

   fputc  ('\x80', po->fpOut);
   xwrite (uLen + 2, 2, po->fpOut);
   xwrite (uLen, 1, po->fpOut);
   fputs  (po->pszInFile, po->fpOut);
   fputc  ('\0', po->fpOut);
   }



/*************************************************************************/
/*                                                                       */
/* LNAMES Stuff                                                          */
/*                                                                       */
/*************************************************************************/


/*
 * This fn returns the name index.
 * The name is added to the name list if needed.
 */
USHORT AddName (PO po, PSZ pszName)
   {
   USHORT i;
   PLNAME pl;

   for (i=0, pl=po->plname; pl && pl->pszName; pl++, i++)
      {
      if (!stricmp (pszName, pl->pszName))
         return pl->uIndex;
      }

   /*--- name not found in list ---*/
   po->plname = realloc (po->plname, sizeof (LNAME) * (i + 2));
   po->plname[i+1].pszName = NULL;
   po->plname[i].pszName = strdup (pszName);
   po->plname[i].uIndex = i+2;               // 1 based index, 1st is empty
   return po->plname[i].uIndex;
   }


/*
 * This fn writes the name list 
 * names include segment and class names
 */
void WriteLNAMES (PO po)
   {
   USHORT i, uLen;

   for (uLen=i=0; po->plname[i].pszName; i++)
      uLen += strlen (po->plname[i].pszName) + 1;

   fputc  ('\x96', po->fpOut);                        // opcode for LNAMES
   xwrite (uLen + 2, 2, po->fpOut);                   // field length

   fputc  ('\0', po->fpOut);                          // first name is empty

   for (i=0; po->plname[i].pszName; i++)
      {
      xwrite (strlen (po->plname[i].pszName), 1, po->fpOut);  // name length
      fputs  (po->plname[i].pszName, po->fpOut);              // name
      }
   fputc  ('\0', po->fpOut);                                  // crc
   }


/*************************************************************************/
/*                                                                       */
/* SEGDEF Stuff                                                          */
/*                                                                       */
/*************************************************************************/


PSEGDEF FindSegment (PO po, USHORT uSegDefIdx)
   {
   USHORT i;
   PSEGDEF pseg;

   for (i=0, pseg=po->pseg; pseg && pseg->pszSeg; pseg++)
      {
      if (pseg->uSegDefIdx == uSegDefIdx)
         return pseg;
      }
   MyError (po, "Unable to find SEGDEF index #%d", uSegDefIdx);
   return NULL;
   }


/*
 * returns the index of a segment
 * this adds the segment to the segment list if it is new
 * if new, the segment name is also added to the name list
 */
USHORT AddSeg (PO po, PSZ pszSeg)
   {
   USHORT  i;

   for (i=0; po->pseg && po->pseg[i].pszSeg; i++)
      if (!stricmp (pszSeg, po->pseg[i].pszSeg))
         return po->pseg[i].uSegDefIdx;

   /*--- name not found in list ---*/
   po->pseg = realloc (po->pseg, sizeof (SEGDEF) * (i + 2));
   po->pseg[i+1].pszSeg   = NULL;

   po->pseg[i].pszSeg      = strdup  (pszSeg);
   po->pseg[i].uSegNameIdx = AddName (po, pszSeg);
   po->pseg[i].uClsNameIdx = uCLASSNAMEIDX;

   po->pseg[i].uSegDefIdx  = i+1;       // index is 1 based;

   switch (uALIGNMENT)
      {
      case 2 : po->pseg[i].uFlags = 0x48; break; // word align
      case 4 : po->pseg[i].uFlags = 0xA8; break; // 4byte align
      case 16: po->pseg[i].uFlags = 0x68; break; // para align
      default: po->pseg[i].uFlags = 0x28; break; // byte align
      }
   po->pseg[i].uFlags      = 0x28;      // fixed for now 28-byte 48-word 68-paragraph
   po->pseg[i].uSegLen     = 0;         // updated as we write data

   return po->pseg[i].uSegDefIdx;
   }


/*
 * This fn writes all the SEGDEF records
 *
 */
void WriteAllSEGDEFs (PO po)
   {
   USHORT i, uSegLen = 0;

   for (i=0; po->pseg[i].pszSeg; i++)
      {
      fwrite ("\x98\x07\x00", 1, 3, po->fpOut);    // opcode & rec len for SEGDEF
      fputc  (po->pseg[i].uFlags, po->fpOut);      // SEGDEF flags; 

      po->pseg[i].ulSegLenPos = ftell (po->fpOut);
      xwrite (po->pseg[i].uSegLen, 2, po->fpOut);  // placemarker

      fputc  (po->pseg[i].uSegNameIdx, po->fpOut); // SEGDEF Seg name idx
      fputc  (po->pseg[i].uClsNameIdx, po->fpOut); // SEGDEF Cls name idx
      fputc  (1, po->fpOut);                       // SEGDEF Overlay
      fputc  (0, po->fpOut);                       // SEGDEF crc
      }
   }


/*
 * go back and update segment sizes
 * Do this after the data is written
 */
void WriteSEGDEFLens (PO po)
   {
   PSEGDEF pseg;
   USHORT  i;
   ULONG   ulFilePos;

   ulFilePos = ftell (po->fpOut);

   for (i=0; po->pseg[i].pszSeg; i++)
      {
      pseg = po->pseg + i;

      /*--- See if we need to do an alignment fix to seg size ---*/
      if (pseg->uSegLen % uALIGNMENT)
         pseg->uSegLen += uALIGNMENT - (pseg->uSegLen % uALIGNMENT);

      fseek (po->fpOut, pseg->ulSegLenPos, SEEK_SET);
      xwrite (pseg->uSegLen, 2, po->fpOut);
      }
   fseek (po->fpOut, ulFilePos, SEEK_SET);
   }


/*************************************************************************/
/*                                                                       */
/*  Vars Stuff                                                           */
/*                                                                       */
/*************************************************************************/


/*
 * This fn writes the name list 
 * names include segment and class names
 *
 * we need one for each segment
 *
 */
void WriteAllPUBDEFs (PO po)
   {
   USHORT uSeg, uEntry, uLen;

   /*--- there will be as many pubdefs as segments ---*/
   for (uSeg=0; po->pseg[uSeg].pszSeg; uSeg++)
      {
      /*--- length of names in this segment ---*/
      for (uLen=uEntry=0; po->pentry[uEntry].pszVar; uEntry++)
         {
         if (po->pentry[uEntry].uSegDefIdx != po->pseg[uSeg].uSegDefIdx)
            continue;
         uLen += strlen (po->pentry[uEntry].pszVar) + 5;
         }
      fputc  ('\x90', po->fpOut);                       // opcode for LNAMES
      xwrite (uLen + 3, 2, po->fpOut);                  // field length
      fputc  (00, po->fpOut);                           // Group Idx
      fputc  (po->pseg[uSeg].uSegDefIdx, po->fpOut);    // Seg Idx

      /*--- write names in this segment ---*/
      for (uLen=uEntry=0; po->pentry[uEntry].pszVar; uEntry++)
         {
         if (po->pentry[uEntry].uSegDefIdx != po->pseg[uSeg].uSegDefIdx)
            continue;

         xwrite (strlen (po->pentry[uEntry].pszVar) + 1, 1, po->fpOut);  // name length
         fputc  ('_', po->fpOut);
         fputs  (po->pentry[uEntry].pszVar, po->fpOut);

         po->pentry[uEntry].ulOffsetPos = ftell (po->fpOut); // store Offset pos for fixup
         xwrite (0, 2, po->fpOut);                      // data offset
         fputc  (0, po->fpOut);                         // data type (not used)
         }
      fputc (0, po->fpOut);                             // checksum
      }
   }


/*
 * go back and update data offsets
 * Do this after the data is written
 *
 */
void WritePUBDEFOffsets (PO po)
   {
   USHORT i;
   ULONG  ulFilePos;

   ulFilePos = ftell (po->fpOut);

   for (i=0; po->pentry[i].pszVar; i++)
      {
      fseek (po->fpOut, po->pentry[i].ulOffsetPos, SEEK_SET);
      xwrite (po->pentry[i].uSegOffset, 2, po->fpOut);
      }
   fseek (po->fpOut, ulFilePos, SEEK_SET);
   }


/*************************************************************************/
/*                                                                       */
/*  LEDATA Stuff                                                         */
/*                                                                       */
/*************************************************************************/


void WriteLEDATA (FILE   *fp, 
                  USHORT uSeg,
                  PSZ    pszBuff, 
                  USHORT uBuffLen,
                  USHORT uOffset)
   {
   fputc  ('\xA0', fp);                      // field opcode
   xwrite (uBuffLen + 4, 2, fp);             // field len
   fputc  (uSeg, fp);                        // seg
   xwrite (uOffset, 2, fp);                  // offset for this data
   dwrite (pszBuff, uBuffLen, fp);           // data
   fputc  ('\0', fp);                        // crc
   }


/*************************************************************************/
/*                                                                       */
/*  GRPDEF & FIXUPP Stuff                                                */
/*                                                                       */
/*************************************************************************/


/*
 * If i can get away with this, I may not need a GRPDEF record
 * this meaning FRAME references a seg not a grp
 *
 */
void WriteTHREAD (PO po, USHORT uEntry)
   {
   USHORT uSegDefIdx;

   uSegDefIdx = po->pentry[uEntry].uSegDefIdx;

   fputc  ('\x9C', po->fpOut);         // field opcode for FIXUPP
   xwrite (5, 2,   po->fpOut);         //  field len
   fputc  ('\x00', po->fpOut);         // TARGET #0 
   fputc  (uSegDefIdx, po->fpOut);     // SEGDEF Idx #1
   fputc  ('\x40', po->fpOut);         // FRAME #0
   fputc  (uSegDefIdx, po->fpOut);     // SEGDEF Idx #1
   fputc  ('\x00', po->fpOut);         // crc
   }


void WriteFIXUPP (FILE *fp, USHORT uFixups)
   {
   USHORT i;

   if (!uFixups)
      return;

   fputc  ('\x9C', fp);                // field opcode
   xwrite (uFixups * 3 + 1, 2, fp);    // field len

   for (i=uFixups; i; i--)
      {
      fputc  (0xCC | ((i-1)*4 >> 8), fp);  // SegRel, 32bit ptr + 2 high offset bits
      fputc  (((i-1)*4 & 0xFF), fp);       // 8 low offset bits
      fputc  ('\x8C', fp);             // target thread 0, frame 0, no displacement
      }
   fputc  ('\0', fp);                  // crc
   }


/*************************************************************************/
/*                                                                       */
/*  ReadDataStuff                                                        */
/*                                                                       */
/*************************************************************************/


PSZ CreateNewSegName (PSZ pszSegName)
   {
   sprintf (pszSegName, "_DATA%3.3d", uNEWSEG++);
   return pszSegName;
   }


/*
 * This fn reads information about an entry in the cfg file.
 *
 * [VarName,DataType,Segment]
 *
 */
BOOL ReadEntry (PO     po,
                FILE   *fp, 
                PSZ    pszVarName, 
                PSZ    pszTypeName, 
                PSZ    pszSegName, 
                PULONG pulDataOffset)
   {
   char szLine [LINESIZE];
   PSZ  *ppsz;
   USHORT uPs;

   if (po->bInLine)   // Then this is NOT a DAT file
      {
      strcpy (pszVarName,  (ArgIs("Variable") ? ArgGet("Variable", 0) : GetFileName (pszVarName, NULL, po->pszInFile, "")));
      strcpy (pszTypeName, (ArgIs("Type")     ? ArgGet("Type", 0)     : "CHAR"));
      strcpy (pszSegName,  (ArgIs("Segment")  ? ArgGet("Segment", 0)  : CreateNewSegName (pszSegName)));
      }
   else
      {
      while (FilReadLine (fp, szLine, ";", LINESIZE) != 0xFFFF)
         if (CfgEndOfSection (szLine))  // look for start of new section
            break;
      if (!*szLine)
         return FALSE;

      StrStrip (StrClip (szLine, " \t]"), " \t[");
      ppsz = StrMakePPSZ (szLine, ",", TRUE, TRUE, &uPs);

      strcpy (pszVarName,  (uPs>0 && ppsz[0] && *ppsz[0] ? ppsz[0] : "Var"));
      strcpy (pszTypeName, (uPs>1 && ppsz[1] && *ppsz[1] ? ppsz[1] : "CHAR"));
      strcpy (pszSegName,  (uPs>2 && ppsz[2] && *ppsz[2] ? ppsz[2] : CreateNewSegName (pszSegName)));

      MemFreePPSZ (ppsz, 0);
      }
   *pulDataOffset = ftell (fp);
   return TRUE;
   }


USHORT GuessDataType (PSZ pszType)
   {
   if (!pszType || !*pszType)               return 0;
   if (!stricmp (pszType, "PSZ"))           return 1;
   if (!stricmp (pszType, "USHORT"))        return 2;
   if (!stricmp (pszType, "SHORT"))         return 2;
   if (!stricmp (pszType, "int"))           return 2;
   if (!stricmp (pszType, "unsigned int"))  return 2;
   if (!stricmp (pszType, "ULONG"))         return 3;
   if (!stricmp (pszType, "LONG"))          return 3;
   if (!stricmp (pszType, "unsigned long")) return 3;
   if (!stricmp (pszType, "BIN"))           return 4;
   if (!stricmp (pszType, "HEX"))           return 4;
   if (!stricmp (pszType, "BYTE"))          return 4;
   return 0; // assume CHAR
   }


USHORT LoadEntrys (PO po)
   {
   PENTRY pentry;
   USHORT i;
   char   szVar [256];
   char   szType[256];
   char   szSeg [256];
   ULONG  ulOffset;

   for (i=0; TRUE; i++)
      {
      po->pentry = realloc (po->pentry, sizeof (ENTRY) * (i + 1));
      pentry = po->pentry + i;
      pentry->pszVar = NULL;

      if (po->bInLine && i)   // If this is NOT a DAT file
         break;               // Then only create one Entry record

      if (!ReadEntry (po, po->fpIn, szVar, szType, szSeg, &ulOffset))
         break;

      pentry->fp        = po->fpIn;
      pentry->ulDataPos = ulOffset;
      pentry->pszVar    = strdup (szVar);
      pentry->pszType   = strdup (szType);
      pentry->pszSeg    = strdup (szSeg);
      pentry->uSegDefIdx= AddSeg (po, szSeg);

      pentry->uDataType = GuessDataType (szType);
      }
   return i;
   }


/*************************************************************************/
/*                                                                       */
/*  Write to OBJ Stuff                                                   */
/*                                                                       */
/*************************************************************************/


/*
 * Reads data from the .DAT file.
 * Returns TRUE if there is more data to read
 *
 */
BOOL ReadPSZData (PO po, USHORT uEntry, PSZ pszBuff, PUSHORT puBuffLen)
   {
   PENTRY pentry;
   FILE   *fp;
   ULONG  ulLinePos;
   USHORT uLineLen;
   char   szLine1[LINESIZE];
   char   szLine2[LINESIZE];
   BOOL   bReturn = FALSE;

   *pszBuff   = '\0';
   *puBuffLen = 0;
   pentry     = po->pentry + uEntry;
   fp         = pentry->fp;

   fseek (fp, pentry->ulDataPos, SEEK_SET);

   while (TRUE)
      {
      ulLinePos = ftell (fp);

      /*--- End of File ---*/
      if (FilReadLine (fp, szLine1, ";", LINESIZE) == 0xFFFF)
         break;

      /*--- End of Section ---*/
      if (!po->bInLine && CfgEndOfSection (szLine1))
         break;

      if (po->bInLine)
         {
         strcpy (szLine2, szLine1);            // actual file don't cook strings
         strcat (szLine2, "\n");
         }
      else
         StrCookLine (szLine2, szLine1, TRUE); // DAT file, cook strings

      uLineLen = strlen (szLine2);

      /*--- End of Room for current read ---*/
      if (*puBuffLen + uLineLen > MAXLESIZE)
         {
         pentry->ulDataPos = ulLinePos;       // backup to before line
         bReturn = TRUE;
         break;
         }
      strcat (pszBuff, szLine2);
      *puBuffLen += uLineLen;
      }
   return bReturn;
   }


BOOL ReadUSHORTData (PO po, USHORT uEntry, PSZ pszBuff, PUSHORT puBuffLen)
   {
   PENTRY pentry;
   PUSHORT pu;
   char   szLine[LINESIZE];
   char   szWord[LINESIZE];
   USHORT uCount, uVal, cDelim = 1;
   PSZ    psz;

   *puBuffLen = 0;
   pu         = (PUSHORT) pszBuff;
   pentry     = po->pentry + uEntry;

   fseek (pentry->fp, pentry->ulDataPos, SEEK_SET);

   if (po->bInLine)
      {
      *puBuffLen = fread (pszBuff, 1, MAXLESIZE, pentry->fp);
      pentry->ulDataPos = ftell (pentry->fp);
      return (*puBuffLen == MAXLESIZE);
      }

   while (TRUE)
      {
      if (FilReadLine (pentry->fp, szLine, ";", LINESIZE) == 0xFFFF)
         return FALSE;   
      if (CfgEndOfSection (szLine))
         return FALSE;
      if (!StrBlankLine (szLine))
         break;
      }
   for (psz = szLine, uCount=0; cDelim; uCount++)
      {
      cDelim = StrGetWord (&psz, szWord, " \t,", " \t,", FALSE, TRUE);
      uVal = atoi (szWord);
      if (!uVal && *szWord != '0')
         MyError (po, "Bad Number format: %s for var: %s", szWord, pentry->pszVar);
      pu[uCount] = uVal;
      }
   *puBuffLen = uCount * 2;
   pentry->ulDataPos = ftell (pentry->fp);
   return TRUE;
   }


BOOL ReadULONGData (PO po, USHORT uEntry, PSZ pszBuff, PUSHORT puBuffLen)
   {
   PENTRY pentry;
   PULONG pul;
   char   szLine[LINESIZE];
   char   szWord[LINESIZE];
   USHORT uCount, cDelim = 1;
   ULONG  ulVal;
   PSZ    psz;

   *puBuffLen = 0;
   pul        = (PULONG) pszBuff;
   pentry     = po->pentry + uEntry;

   fseek (pentry->fp, pentry->ulDataPos, SEEK_SET);

   if (po->bInLine)
      {
      *puBuffLen = fread (pszBuff, 1, MAXLESIZE, pentry->fp);
      pentry->ulDataPos = ftell (pentry->fp);
      return (*puBuffLen == MAXLESIZE);
      }

   while (TRUE)
      {
      if (FilReadLine (pentry->fp, szLine, ";", LINESIZE) == 0xFFFF)
         return FALSE;   
      if (CfgEndOfSection (szLine))
         return FALSE;
      if (!StrBlankLine (szLine))
         break;
      }
   for (psz = szLine, uCount=0; cDelim; uCount++)
      {
      cDelim = StrGetWord (&psz, szWord, " \t,", " \t,", FALSE, TRUE);
      ulVal  = atol (szWord);
      if (!ulVal && *szWord != '0')
         MyError (po, "Bad Number format: %s for var: %s", szWord, pentry->pszVar);
      pul[uCount] = ulVal;
      }
   *puBuffLen = uCount * 4;
   pentry->ulDataPos = ftell (pentry->fp);
   return TRUE;
   }


BOOL ReadBINData (PO po, USHORT uEntry, PSZ pszBuff, PUSHORT puBuffLen)
   {
   PENTRY pentry;
   char   szLine[LINESIZE];
   char   szWord[LINESIZE];
   USHORT uVal, uCount, cDelim = 1;
   PSZ    psz;

   *puBuffLen = 0;
   pentry     = po->pentry + uEntry;

   fseek (pentry->fp, pentry->ulDataPos, SEEK_SET);

   if (po->bInLine)
      {
      *puBuffLen = fread (pszBuff, 1, MAXLESIZE, pentry->fp);
      pentry->ulDataPos = ftell (pentry->fp);
      return (*puBuffLen == MAXLESIZE);
      }

   while (TRUE)
      {
      if (FilReadLine (pentry->fp, szLine, ";", LINESIZE) == 0xFFFF)
         return FALSE;   
      if (CfgEndOfSection (szLine))
         return FALSE;
      if (!StrBlankLine (szLine))
         break;
      }
   for (psz = StrClip(szLine, " \t"), uCount=0; cDelim; uCount++)
      {
      cDelim = StrGetWord (&psz, szWord, " \t,", " \t,", FALSE, TRUE);
      uVal = hexval (szWord);
      if (!uVal && *szWord != '0')
         MyError (po, "Bad Number format: %s for var: %s", szWord, pentry->pszVar);
      pszBuff[uCount] = (char)uVal;
      }
   *puBuffLen = uCount;
   pentry->ulDataPos = ftell (pentry->fp);
   return TRUE;
   }


/*
 * Reads a single text line
 * converts \ in strings
 * return FALSE if there is no line to read
 * assumes fp is in the right place
 */
BOOL ReadPPSZData (PO po, USHORT uEntry, PSZ pszBuff, PUSHORT puDataLen)
   {
   FILE   *fp;
   char   szLine1[LINESIZE];

   *pszBuff   = '\0';
   *puDataLen = 0;
   fp = po->pentry[uEntry].fp;

   if (FilReadLine (fp, szLine1, ";", LINESIZE) == 0xFFFF)
      return FALSE;   

   /*--- End of Section ---*/
   if (!po->bInLine && CfgEndOfSection (szLine1))
      return FALSE;

   if (po->bInLine)
      strcpy (pszBuff, szLine1);            // actual file don't cook strings
   else
      StrCookLine (pszBuff, szLine1, FALSE); // DAT file, cook strings

   *puDataLen = strlen (pszBuff);
   return TRUE;
   }


/*
 * This fn writes the LEDATA blocks associated with an indirect variable
 * segment sizes and variable data offsets are kept current
 *
 * This fn actually writes a few field types:
 *    FIXUPP THREAD field to prepare for a fixupp record
 *    LEDATA fields for the data strings (1 per line)
 *    LEDATA fields for the PPSZ pointer array (1 per 250 elements)
 *    FIXUPP FIXUPP fields to change ptr offsets to point within
 *       the current segment (1 per 250 elements)
 */
void WriteINDIRECTVariable (PO po, USHORT uEntry)
   {
   PSEGDEF pseg;
   PENTRY  pentry;
   USHORT  uDataLen, uPPSZLines;
   USHORT  uStart, uArrayEnd, uFixupEnd;

   pentry = po->pentry + uEntry;

   fseek (pentry->fp, pentry->ulDataPos, SEEK_SET);

   WriteTHREAD     (po, uEntry);

   /*--- find segment that goes with this var ---*/
   pseg = FindSegment (po, pentry->uSegDefIdx);

   /*--- See if we need to do an alignment fix ---*/
   if (pseg->uSegLen % uALIGNMENT)
      pseg->uSegLen += uALIGNMENT - (pseg->uSegLen % uALIGNMENT);

   /*--- for PSZ[]'s we also store location of strings ---*/
   pentry->uSegSOffset = pseg->uSegLen;

   /* Write PSZ lines
    * There will be one LEDATA for each line
    * Offsets to each line are stored in the uOffsets[] array
    */
   for (uPPSZLines = 0; TRUE; uPPSZLines++)
      {
      if (!ReadPPSZData (po, uEntry, szBuff, &uDataLen))
         break;

      /*--- add 1 for trailing \0 at end ---*/
      uDataLen += 1; 

      if (uPPSZLines >= MAXPPSZLINES)
         MyError (po, "Too many lines for PPSZ. Var: %s", pentry->pszVar);

      if (pseg->uSegLen > 65535U - uDataLen)
         MyError (po, "Data exceeds 64k.  Var: %s", pentry->pszVar);

      WriteLEDATA (po->fpOut, pseg->uSegDefIdx, szBuff, uDataLen, pseg->uSegLen);
      ulOffsets[uPPSZLines] = pseg->uSegLen;

      /*--- update segment offset & segment length ---*/
      pseg->uSegLen += uDataLen;
      }
   ulOffsets[uPPSZLines++] = 0;

   /*--- See if we need to do an alignment fix ---*/
   if (pseg->uSegLen % uALIGNMENT)
      pseg->uSegLen += uALIGNMENT - (pseg->uSegLen % uALIGNMENT);

   /*--- tell the entry where in the seg his data will reside ---*/
   pentry->uSegOffset = pseg->uSegLen;
   pentry->uDataLen = uPPSZLines * 4;

   /*--- one LEDATA for each 250 array elements     ---*/
   /*--- one FIXUPP for each 250 array elements ---*/
   /*--- write an extra entry for the trailing NULL ---*/
   /*--- do not fixup the trailing NULL ---*/

   for (uStart = 0; uStart < uPPSZLines; uStart = uArrayEnd+1)
      {
      uArrayEnd = min (uPPSZLines-1, uStart + 249);
      uFixupEnd = min (uPPSZLines-2, uStart + 249);

      WriteLEDATA (po->fpOut, 
                   pseg->uSegDefIdx, 
                   (PSZ)(ulOffsets + uStart), 
                   (uArrayEnd - uStart + 1) * 4, 
                   pseg->uSegLen);

      WriteFIXUPP (po->fpOut, (uFixupEnd - uStart + 1));

      if (pseg->uSegLen > 65535U - (uArrayEnd - uStart + 1) * 4)
         MyError (po, "Data exceeds 64k.  Var: %s", pentry->pszVar);
      pseg->uSegLen += (uArrayEnd - uStart + 1) * 4;
      }
   }


/*
 * This fn writes the LEDATA blocks associated with a direct variable
 * segment sizes and variable data offsets are kept current
 */
void WriteDIRECTVariable (PO po, USHORT uEntry)
   {
   PSEGDEF pseg;
   PENTRY  pentry;
   BOOL    bWorking;
   USHORT  uField, uDataLen;

   pentry = po->pentry + uEntry;

   /*--- find segment that goes with this var ---*/
   pseg = FindSegment (po, pentry->uSegDefIdx);

   /*--- See if we need to do an alignment fix ---*/
   if (pseg->uSegLen % uALIGNMENT)
      pseg->uSegLen += uALIGNMENT - (pseg->uSegLen % uALIGNMENT);

   /*--- tell the entry where in the seg his data will reside ---*/
   pentry->uSegOffset = pseg->uSegLen;
   pentry->uDataLen = 0;

   for (bWorking=TRUE, uField=0; bWorking; uField++)
      {
      switch (pentry->uDataType)
         {
         case  1: MyError (po, "I Should not be here!");                     break;
         case  2: bWorking = ReadUSHORTData(po, uEntry, szBuff, &uDataLen);  break;
         case  3: bWorking = ReadULONGData (po, uEntry, szBuff, &uDataLen);  break;
         case  4: bWorking = ReadBINData   (po, uEntry, szBuff, &uDataLen);  break;
         default: bWorking = ReadPSZData   (po, uEntry, szBuff, &uDataLen);
         }

      /*--- conditionally add 1 for trailing \0 at end of PSZ ---*/
      uDataLen += (!bWorking && !pentry->uDataType);

      pentry->uDataLen += uDataLen;

      WriteLEDATA (po->fpOut, pseg->uSegDefIdx, szBuff, uDataLen, pseg->uSegLen);

      /*--- update segment offset & segment length ---*/
      if (pseg->uSegLen >=  65535U - uDataLen)
         MyError (po, "Data Segment exceeds 64k. Var: %s", pentry->pszVar);
      pseg->uSegLen += uDataLen;
      }
   }


/*
 * Creates the object file
 *
 */
void MakeObj (PO po)
   {
   USHORT   uEntry;
   PSZ      pszMode;

   pszMode = (po->bInLine && GuessDataType(ArgGet("Type",0)) > 1 ? "rb" : "rt");

   if (!(po->fpIn = fopen (po->pszInFile, pszMode)))
      Error ("Unable to open input file %s", po->pszInFile);
   if (!(po->fpOut = fopen (po->pszOutFile, "wb")))
      Error ("Unable to open output file %s", po->pszOutFile);

   uCLASSNAMEIDX = AddName (po, "DATA");

   if (!LoadEntrys (po))
      MyError (po, "%s: No variables to write", po->pszOutFile);

   WriteTHEADR     (po);
   WriteLNAMES     (po);
   WriteAllSEGDEFs (po);
   WriteAllPUBDEFs (po);
   WriteCOMENT     (po);

   /*--- each var will have its own LEDATA block ---*/
   for (uEntry=0; po->pentry[uEntry].pszVar; uEntry++)
      {
      if (po->pentry[uEntry].uDataType == 1)
         WriteINDIRECTVariable (po, uEntry);   // ppsz is more complicated
      else
         WriteDIRECTVariable (po, uEntry);
      }
   WriteMODEND (po);
   WriteSEGDEFLens (po);
   WritePUBDEFOffsets (po);
   fclose (po->fpIn);
   fclose (po->fpOut);
   }



/* 
 * Creates a header file for
 * this object file.
 */
void MakeHeader (PO po)
   {
   FILE   *fp;
   USHORT uEntry, uCt, uVarLen, uTypeLen;
   PENTRY pentry;
   char   szTmp [80];

   if (!(fp = fopen (po->pszHFile, "wt")))
      MyError (po, "Unable to open Header file %s", po->pszHFile);

   fprintf (fp, "/*\n * %s\n", po->pszHFile);
   fprintf (fp, " * %s\n *\n", __DATE__);
   fprintf (fp, " * This header file was created by the Data2Obj data compiler\n");
   fprintf (fp, " *\n */\n\n");

   uTypeLen=uVarLen=0;
   for (uEntry=0; bSIZES && po->pentry[uEntry].pszVar; uEntry++)
      {
      uVarLen  = max (uVarLen,  strlen (po->pentry[uEntry].pszVar));
      uTypeLen = max (uTypeLen, strlen (po->pentry[uEntry].pszType));
      }

   /*--- write variable size defines (if option chosen ---*/
   for (uEntry=0; bSIZES && po->pentry[uEntry].pszVar; uEntry++)
      {
      pentry = po->pentry + uEntry;
      switch (pentry->uDataType)
         {
         case  1: uCt = pentry->uDataLen/4;  break;     // PSZ   
         case  2: uCt = pentry->uDataLen/2;  break;     // USHORT
         case  3: uCt = pentry->uDataLen/4;  break;     // ULONG 
         case  4: uCt = pentry->uDataLen;    break;     // CHAR  
         default: uCt = pentry->uDataLen;    break;     // CHAR  
         }
      sprintf (szTmp, "%s%s", pentry->pszVar, "LEN");
      fprintf (fp, "#define %-*s %uU\n", uVarLen+4, szTmp, uCt);
      }
   fprintf (fp, "\n");


   /*--- write external variable declarations ---*/
   for (uEntry=0; po->pentry[uEntry].pszVar; uEntry++)
      {
      pentry = po->pentry + uEntry;
      switch (pentry->uDataType)
         {
         case  1: uCt = pentry->uDataLen/4;  break;
         case  2: uCt = pentry->uDataLen/2;  break;
         case  3: uCt = pentry->uDataLen/4;  break;
         case  4: uCt = pentry->uDataLen;    break;
         default: uCt = pentry->uDataLen;    break;
         }
      fprintf (fp, "extern %-*s %s[%uU];\n", uTypeLen, pentry->pszType, pentry->pszVar, uCt);
      }
   fprintf (fp, "\n");
   }


/*
 * Dumps a listing of all the variables
 * to stdout.
 *
 */
void ListVars (PO po)
   {
   PENTRY pentry;
   char   szTmp [80];
   USHORT i, uCt, uEntry, uTypeLen=4, uVarLen=7, uSegLen=7;

   for (uEntry=0; po->pentry[uEntry].pszVar; uEntry++)
      {
      pentry = po->pentry + uEntry;
      switch (po->pentry[uEntry].uDataType)
         {
         case  1: uCt = pentry->uDataLen/4;  break;     // PSZ   
         case  2: uCt = pentry->uDataLen/2;  break;     // USHORT
         case  3: uCt = pentry->uDataLen/4;  break;     // ULONG 
         case  4: uCt = pentry->uDataLen;    break;     // CHAR  
         default: uCt = pentry->uDataLen;    break;     // CHAR  
         }
      sprintf (szTmp, "%s[%u]", pentry->pszVar, uCt); 
      uTypeLen = max (uTypeLen, strlen (pentry->pszType));
      uVarLen  = max (uVarLen,  strlen (szTmp));
      uSegLen  = max (uSegLen,  strlen (pentry->pszSeg));
      }
   printf ("%-*s %-*s   Size  %-*s  Offset\n", uTypeLen, "Type",
                                               uVarLen,  "VarName",
                                               uSegLen,  "Segment");
   
   for (i= uTypeLen + uVarLen + uSegLen + 24; i; i--)
      printf ("-");
   printf ("\n");

   for (uEntry=0; po->pentry[uEntry].pszVar; uEntry++)
      {
      pentry = po->pentry + uEntry;
      switch (po->pentry[uEntry].uDataType)
         {
         case  1: uCt = pentry->uDataLen/4;  break;     // PSZ   
         case  2: uCt = pentry->uDataLen/2;  break;     // USHORT
         case  3: uCt = pentry->uDataLen/4;  break;     // ULONG 
         case  4: uCt = pentry->uDataLen;    break;     // CHAR  
         default: uCt = pentry->uDataLen;    break;     // CHAR  
         }
      sprintf (szTmp, "%s[%u]", pentry->pszVar, uCt); 
      printf ("%-*s %-*s  %5u  %-*s  %5u (%4.4X)",  
            uTypeLen, pentry->pszType,
            uVarLen,  szTmp,
            pentry->uDataLen,
            uSegLen,  pentry->pszSeg,
            pentry->uSegOffset, pentry->uSegOffset);

      if (po->pentry[uEntry].uDataType == 1) // PSZ[]
         printf (" Strings Start at %5u (%4.4X)",
            pentry->uSegSOffset, pentry->uSegSOffset);

      printf ("\n");
      }
   printf ("\n");
   }


void Banner (void)
   {
   printf ("Data2Obj  Data File Compiler v2.1           Craig Fitzgerald     %s\n\n", __DATE__);
   }



void Usage (void)
   {
   Banner ();
   printf ("USAGE#1:  Data2Obj [options1] datfile [objfile]\n\n");
   printf ("USAGE#2:  Data2Obj [options2] /Source=Sourcefile [objfile]\n\n");
   printf ("WHERE:  datfile .... Is the name of the file containing variable declarations.\n");
   printf ("        objfile .... If specified, is the name of the object file.\n");
   printf ("                     If not given, the input file + '.obj' is used.\n\n");
   printf ("        [options1] are 0 or more of:\n");
   printf ("           /? ................ This help.\n");
   printf ("           /HelpDat .......... Help with the datfile format.\n");
   printf ("           /Header[=hname] ... Create Header file, hname is optional.\n");
   printf ("           /Sizes ............ Create variable size #defines in header.\n");
   printf ("           /Verbose .......... Display info during compile.\n");
   printf ("           /Alignment=# ...... Align Data on 1,2,4 or 16 byte boundries.\n");
   printf ("           /StartSeg=# ....... 1st created segment will have this index.\n");
   printf ("           /Debug ............ Debug stuff, currently does nothing.\n");
   printf ("\n");
   printf ("        [options2] are the same as [options1] plus:\n");
   printf ("           /Variable=varname . Variable Name (Default is Var).\n");
   printf ("           /SegName=segname .. Segment Name  (Default is a new seg).\n");
   printf ("           /Type=typename .... Data Type (Default is CHAR).\n");
   printf ("\n");
   printf ("Usage #1 uses a DAT file to gather information and data for variables.  A DAT\n");
   printf ("file is structured like a INI file.  Standard C character conversions of the\n");
   printf ("form \\n and \\x1A are performed on the text in this file.  Type\n");
   printf ("'Data2Obj /HelpDat' for the format of the dat file.\n");
   printf ("\n");
   printf ("Usage #2 uses an entire file as the contents of a variable.  Variable\n");
   printf ("information must be specified on the command line.  No character conversions\n");
   printf ("are performed on the variable data.\n");
   printf ("\n");
   exit (0);
   }

void DatUsage (void)
   {
   Banner ();
   printf ("DAT file format:\n");
   printf ("\n");
   printf (" This file is the format of an INI file.  This file is broken into sections.\n");
   printf (" Each section describes a single variable.  A variable section is defined as\n");
   printf (" follows:\n");
   printf ("\n");
   printf (" [VarName,VarType,Segment]\n");
   printf (" Content\n");
   printf (" ...\n");
   printf (" \n");
   printf (" VarName - A required field that is the Variables Name.\n");
   printf (" VarType - An optional field specifying the Datatype. The variable will\n");
   printf ("           be an array of this data type. If not specified, CHAR is assumed.\n");
   printf ("           The types are: CHAR, PSZ, USHORT, ULONG, and BYTE.\n");
   printf (" Segment - An optional field that tells where to put the variable. By default\n");
   printf ("           each var is put in a new segment.\n");
   printf (" Content - One or more lines of data that is assigned to the variable.\n");
   printf ("           for CHAR arrays, \\n are appended after each line.\n");
   printf ("           for PSZ arrays, each line is assigned to each ptr, last ptr is NULL.\n");
   printf ("           for USHORT arrays, each line must contain numbers in decimal.\n");
   printf ("           for ULONG arrays, each line must contain numbers in decimal.\n");
   printf ("           for BYTE arrays, each line must contain numbers in Hex from 0-FF.\n");
   printf ("\n");
   printf ("  Lines beginning with a ';' are comment lines. \\ sequences are supported.\n");
   printf ("  for CHAR arrays, \\ at the end of the line mean do not append \\n at line end.\n");
   printf ("\n");
   printf ("  EXAMPLE:\n");
   printf ("\n");
   printf ("  [pszHello]\n");
   printf ("  Get Your filthy Mits out of the cereal!\n");
   printf ("  You stinking bag of protoplasm!\n");
   printf ("  [ppszNames,PSZ]\n");
   printf ("  Ren Hoek\n");
   printf ("  Stimpson J. Cat\n");
   printf ("  Stinky Weaselteats\n");
   printf ("  [uIQs,USHORT]\n");
   printf ("  70 17 24 10 25\n");
   printf ("  60 22 14 42\n");
   printf ("  [bHorkDat,BYTE]\n");
   printf ("  FF 2D 26 7B 8D 66\n");
   printf ("  [pszBye]\n");
   printf ("  \\[His Reply\\]\n");
   printf ("  Youuuuu! \\\n");
   printf ("  I Should of Known!\n");
   printf ("\n");
   exit (0);
   }







/*
 * 2 ways to use this program:
 *
 * Data2Obj [options] DataFile[.DAT] [objfile]       (TYPE 1)
 *  [options]:
 *      /Debug
 *      /Verbose
 *      /Header
 *      /Sizes
 *
 *               -or- 
 *
 * Data2Obj [options] /Source=srcfile [objfile]      (TYPE 2)
 *  [options] same as above plus:
 *       /Variable=varname
 *       /SegName=segname
 *       /Type=typename
 *
 *
 */
int _cdecl main (int argc, char *argv[])
   {
   char szInFile  [256];
   char szOutFile [256];
   char szHFile   [256];
   PO   po;

   if (ArgBuildBlk ("? *^Debug *^Verbose *^Header? *^Sizes "
                    "*^Source% *^Variable% *^Segment% *^Type% "
                    "*^Alignment% *^HelpDat *^StartSeg%"))
      Error ("%s", ArgGetErr ());

   if (ArgFillBlk (argv))
      Error ("%s", ArgGetErr ());

   if (ArgIs ("HelpDat"))
      DatUsage ();

   if (ArgIs ("?") || (!ArgIs (NULL) && !ArgIs ("Source")))
      Usage ();

   bDEBUG   = ArgIs ("Debug"  );
   bVERBOSE = ArgIs ("Verbose");
   bHEADER  = ArgIs ("Header" );
   bSIZES   = ArgIs ("Sizes");
   bINLINE  = ArgIs ("Source");  // which command line type are we using ?
   uNEWSEG = (ArgIs ("StartSeg") ? atoi (ArgGet ("StartSeg", 0)) : 0);

   uALIGNMENT = 1;
   if (ArgIs ("Alignment"))
      if (!(uALIGNMENT = atoi (ArgGet ("Alignment", 0))))
         Error ("Alignment must be 1 2 or 4");

   if (bINLINE)
      {
      /*--- We are using the TYPE 2 command line options) ---*/
      GetFileName (szInFile,  ArgGet ("Source", 0), NULL,     NULL);
      GetFileName (szOutFile, ArgGet (NULL, 0),     szInFile, ".OBJ");
      GetFileName (szHFile,   ArgGet ("Header", 0), szInFile, ".H"  );
      }
   else
      {
      /*--- We are using the TYPE 1 command line options) ---*/
      GetFileName (szInFile,  ArgGet (NULL, 0),     NULL,     ".DAT");
      GetFileName (szOutFile, ArgGet (NULL, 1),     szInFile, ".OBJ");
      GetFileName (szHFile,   ArgGet ("Header", 0), szInFile, ".H"  );
      }

   po = malloc (sizeof (O));
   po->pszInFile  = szInFile;
   po->pszOutFile = szOutFile;
   po->pszHFile   = szHFile;
   po->plname     = NULL;
   po->pseg       = NULL;
   po->pentry     = NULL;
   po->bInLine    = bINLINE;

   MakeObj (po);

   if (bHEADER)
      MakeHeader (po);

   if (bVERBOSE)
      ListVars (po);

   return 0;
   }

