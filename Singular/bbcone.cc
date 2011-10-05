#include <Singular/mod2.h>

#ifdef HAVE_FANS

#include <Singular/ipid.h>
#include <Singular/blackbox.h>
#include <omalloc/omalloc.h>
#include <kernel/febase.h>
#include <kernel/longrat.h>
#include <Singular/subexpr.h>
#include <gfanlib/gfanlib.h>
#include <kernel/bbcone.h>
#include <ipshell.h>
#include <kernel/intvec.h>
#include <sstream>


int coneID;

int integerToInt(gfan::Integer const &V, bool &ok)
{
  mpz_t v;
  mpz_init(v);
  V.setGmp(v);
  int ret=0;
  if(mpz_fits_sint_p(v))
    ret=mpz_get_si(v);
  else
    ok=false;
  mpz_clear(v);
  return ret;
}

intvec* zVector2Intvec(const gfan::ZVector zv)
{
  int d=zv.size();
  intvec* iv = new intvec(1, d, 0);
  bool ok = true;
  for(int i=1;i<=d;i++)
    IMATELEM(*iv, 1, i) = integerToInt(zv[i-1], ok);
  if (!ok) WerrorS("overflow while converting a gfan::ZVector to an intvec");
  return iv;
}

intvec* zMatrix2Intvec(const gfan::ZMatrix zm)
{
  int d=zm.getHeight();
  int n=zm.getWidth();
  intvec* iv = new intvec(d, n, 0);
  bool ok = true;
  for(int i=1;i<=d;i++)
    for(int j=1;j<=n;j++)
      IMATELEM(*iv, i, j) = integerToInt(zm[i-1][j-1], ok);
  if (!ok) WerrorS("overflow while converting a gfan::ZMatrix to an intmat");
  return iv;
}

gfan::ZMatrix intmat2ZMatrix(const intvec* iMat)
{
  int d=iMat->rows();
  int n=iMat->cols();
  gfan::ZMatrix ret(d,n);
  for(int i=0;i<d;i++)
    for(int j=0;j<n;j++)
      ret[i][j]=IMATELEM(*iMat, i+1, j+1);
  return ret;
}

/* expects iMat to have just one row */
gfan::ZVector intvec2ZVector(const intvec* iVec)
{
  int n =iVec->rows();
  gfan::ZVector ret(n);
  for(int j=0;j<n;j++)
    ret[j]=IMATELEM(*iVec, j+1, 1);
  return ret;
}

std::string toString(gfan::ZMatrix const &m, char *tab=0)
{
  std::stringstream s;

  for(int i=0;i<m.getHeight();i++)
    {
      if(tab)s<<tab;
      for(int j=0;j<m.getWidth();j++)
	{
	  s<<m[i][j];
	  if(i+1!=m.getHeight() || j+1!=m.getWidth())
	    {
	      s<<",";
	    }
	}
      s<<std::endl;
    }
  return s.str();
}

std::string toString(gfan::ZCone const &c)
{
  std::stringstream s;
  gfan::ZMatrix i=c.getInequalities();
  gfan::ZMatrix e=c.getEquations();
  s<<"AMBIENT_DIM"<<std::endl;
  s<<c.ambientDimension()<<std::endl;
  s<<"INEQUALITIES"<<std::endl;
  s<<toString(i);
  s<<"EQUATIONS"<<std::endl;
  s<<toString(e);
  return s.str();
}

void *bbcone_Init(blackbox *b)
{
  return (void*)(new gfan::ZCone());
}

BOOLEAN bbcone_Assign(leftv l, leftv r)
{
  gfan::ZCone* newZc;
  if (r==NULL)
  { 
    if (l->Data()!=NULL) 
    {   
      gfan::ZCone* zd = (gfan::ZCone*)l->Data();
      delete zd;
    }
    newZc = new gfan::ZCone();
  }
  else if (r->Typ()==l->Typ())
  {
    if (l->Data()!=NULL)
    {   
      gfan::ZCone* zd = (gfan::ZCone*)l->Data();
      delete zd;
    }
    gfan::ZCone* zc = (gfan::ZCone*)r->Data();
    newZc = new gfan::ZCone(*zc);
  }
  else if (r->Typ()==INT_CMD)
  {
    int ambientDim = (int)(long)r->Data();
    if (ambientDim < 0)
    {
      Werror("expected an int >= 0, but got %d", ambientDim);
      return TRUE;
    }
    if (l->Data()!=NULL)
    {   
      gfan::ZCone* zd = (gfan::ZCone*)l->Data();
      delete zd;
    }
    newZc = new gfan::ZCone(ambientDim);
  }
  else
  {
    Werror("assign Type(%d) = Type(%d) not implemented",l->Typ(),r->Typ());
    return TRUE;
  }
 
  if (l->rtyp==IDHDL)
  {
    IDDATA((idhdl)l->data)=(char *)newZc;
  }
  else
  {
    l->data=(void *)newZc;
  }
  return FALSE;
}

char * bbcone_String(blackbox *b, void *d)
{ if (d==NULL) return omStrDup("invalid object");
   else
   {
     std::string s=toString(*((gfan::ZCone*)d));
     char* ns = (char*) omAlloc(strlen(s.c_str()) + 10);
     sprintf(ns, "%s", s.c_str());
     omCheckAddr(ns);
     return ns;
   }
}

void bbcone_destroy(blackbox *b, void *d)
{
  if (d!=NULL)
  {
    gfan::ZCone* zc = (gfan::ZCone*) d;
    delete zc;
  }
}

void * bbcone_Copy(blackbox*b, void *d)
{       
  gfan::ZCone* zc = (gfan::ZCone*)d;
  gfan::ZCone* newZc = new gfan::ZCone(*zc);
  return newZc;
}

static BOOLEAN jjCONERAYS1(leftv res, leftv v)
{
  /* method for generating a cone object from half-lines
     (cone = convex hull of the half-lines; note: there may be
     entire lines in the cone);
     valid parametrizations: (intmat) */
  intvec* rays = (intvec *)v->CopyD(INTVEC_CMD);
  gfan::ZMatrix zm = intmat2ZMatrix(rays);
  gfan::ZCone* zc = new gfan::ZCone();
  *zc = gfan::ZCone::givenByRays(zm, gfan::ZMatrix(0, zm.getWidth()));
  res->rtyp = coneID;
  res->data = (char *)zc;
  return FALSE;
}

static BOOLEAN jjCONERAYS2(leftv res, leftv u, leftv v)
{
  /* method for generating a cone object from half-lines,
     and lines (any point in the cone being the sum of a point
     in the convex hull of the half-lines and a point in the span
     of the lines; the second argument may contain or entirely consist
     of zero rows);
     valid parametrizations: (intmat, intmat)
     Errors will be invoked in the following cases:
     - u and v have different numbers of columns */
  intvec* rays = (intvec *)u->CopyD(INTVEC_CMD);
  intvec* linSpace = (intvec *)v->CopyD(INTVEC_CMD);
  if (rays->cols() != linSpace->cols())
  {
    Werror("expected same number of columns but got %d vs. %d",
           rays->cols(), linSpace->cols());
    return TRUE;
  }
  gfan::ZMatrix zm1 = intmat2ZMatrix(rays);
  gfan::ZMatrix zm2 = intmat2ZMatrix(linSpace);
  gfan::ZCone* zc = new gfan::ZCone();
  *zc = gfan::ZCone::givenByRays(zm1, zm2);
  res->rtyp = coneID;
  res->data = (char *)zc;
  return FALSE;
}

static BOOLEAN jjCONERAYS3(leftv res, leftv u, leftv v, leftv w)
{
  /* method for generating a cone object from half-lines,
     and lines (any point in the cone being the sum of a point
     in the convex hull of the half-lines and a point in the span
     of the lines), and an integer k;
     valid parametrizations: (intmat, intmat, int);
     Errors will be invoked in the following cases:
     - u and v have different numbers of columns,
     - k not in [0..3];
     if the 2^0-bit of k is set, then the lineality space is known
     to be the span of the provided lines;
     if the 2^1-bit of k is set, then the extreme rays are known:
     each half-line spans a (different) extreme ray */
  intvec* rays = (intvec *)u->CopyD(INTVEC_CMD);
  intvec* linSpace = (intvec *)v->CopyD(INTVEC_CMD);
  if (rays->cols() != linSpace->cols())
  {
    Werror("expected same number of columns but got %d vs. %d",
           rays->cols(), linSpace->cols());
    return TRUE;
  }
  int k = (int)(long)w->Data();
  if ((k < 0) || (k > 3))
  {
    WerrorS("expected int argument in [0..3]");
    return TRUE;
  }
  gfan::ZMatrix zm1 = intmat2ZMatrix(rays);
  gfan::ZMatrix zm2 = intmat2ZMatrix(linSpace);
  gfan::ZCone* zc = new gfan::ZCone();
  *zc = gfan::ZCone::givenByRays(zm1, zm2);
  //k should be passed on to zc; not available yet
  res->rtyp = coneID;
  res->data = (char *)zc;
  return FALSE;
}

BOOLEAN cone_via_rays(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == INTMAT_CMD))
  {
    if (u->next == NULL) return jjCONERAYS1(res, u);
  }
  leftv v = u->next;
  if ((v != NULL) && (v->Typ() == INTMAT_CMD))
  {
    if (v->next == NULL) return jjCONERAYS2(res, u, v);
  }
  leftv w = v->next;
  if ((w != NULL) && (w->Typ() == INT_CMD))
  {
    if (w->next == NULL) return jjCONERAYS3(res, u, v, w);
  }
  WerrorS("cone_via_rays: unexpected parameters");
  return TRUE;
}

static BOOLEAN jjCONENORMALS1(leftv res, leftv v)
{
  /* method for generating a cone object from inequalities;
     valid parametrizations: (intmat) */
  intvec* inequs = (intvec *)v->CopyD(INTVEC_CMD);
  gfan::ZMatrix zm = intmat2ZMatrix(inequs);
  gfan::ZCone* zc = new gfan::ZCone(zm, gfan::ZMatrix(0, zm.getWidth()));
  res->rtyp = coneID;
  res->data = (char *)zc;
  return FALSE;
}

static BOOLEAN jjCONENORMALS2(leftv res, leftv u, leftv v)
{
  /* method for generating a cone object from iequalities,
     and equations (...)
     valid parametrizations: (intmat, intmat)
     Errors will be invoked in the following cases:
     - u and v have different numbers of columns */
  intvec* inequs = (intvec *)u->CopyD(INTVEC_CMD);
  intvec* equs = (intvec *)v->CopyD(INTVEC_CMD);
  if (inequs->cols() != equs->cols())
  {
    Werror("expected same number of columns but got %d vs. %d",
           inequs->cols(), equs->cols());
    return TRUE;
  }
  gfan::ZMatrix zm1 = intmat2ZMatrix(inequs);
  gfan::ZMatrix zm2 = intmat2ZMatrix(equs);
  gfan::ZCone* zc = new gfan::ZCone(zm1, zm2);
  res->rtyp = coneID;
  res->data = (char *)zc;
  return FALSE;
}

static BOOLEAN jjCONENORMALS3(leftv res, leftv u, leftv v, leftv w)
{
  /* method for generating a cone object from inequalities, equations,
     and an integer k;
     valid parametrizations: (intmat, intmat, int);
     Errors will be invoked in the following cases:
     - u and v have different numbers of columns,
     - k not in [0..3];
     if the 2^0-bit of k is set, then ... */
  intvec* inequs = (intvec *)u->CopyD(INTVEC_CMD);
  intvec* equs = (intvec *)v->CopyD(INTVEC_CMD);
  if (inequs->cols() != equs->cols())
  {
    Werror("expected same number of columns but got %d vs. %d",
           inequs->cols(), equs->cols());
    return TRUE;
  }
  int k = (int)(long)w->Data();
  if ((k < 0) || (k > 3))
  {
    WerrorS("expected int argument in [0..3]");
    return TRUE;
  }
  gfan::ZMatrix zm1 = intmat2ZMatrix(inequs);
  gfan::ZMatrix zm2 = intmat2ZMatrix(equs);
  gfan::ZCone* zc = new gfan::ZCone(zm1, zm2, k);
  res->rtyp = coneID;
  res->data = (char *)zc;
  return FALSE;
}

BOOLEAN cone_via_normals(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == INTMAT_CMD))
  {
    if (u->next == NULL) return jjCONENORMALS1(res, u);
  }
  leftv v = u->next;
  if ((v != NULL) && (v->Typ() == INTMAT_CMD))
  {
    if (v->next == NULL) return jjCONENORMALS2(res, u, v);
  }
  leftv w = v->next;
  if ((w != NULL) && (w->Typ() == INT_CMD))
  {
    if (w->next == NULL) return jjCONENORMALS3(res, u, v, w);
  }
  WerrorS("cone_via_normals: unexpected parameters");
  return TRUE;
}

BOOLEAN getInequalities(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->getInequalities();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getInequalities: unexpected parameters");
  return TRUE;
}

BOOLEAN getEquations(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->getEquations();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getEquations: unexpected parameters");
  return TRUE;
}

BOOLEAN getFacets(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->getFacets();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getFacets: unexpected parameters");
  return TRUE;
}

BOOLEAN getImpliedEquations(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->getImpliedEquations();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getImpliedEquations: unexpected parameters");
  return TRUE;
}

BOOLEAN getGeneratorsOfSpan(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->generatorsOfSpan();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getGeneratorsOfSpan: unexpected parameters");
  return TRUE;
}

BOOLEAN getGeneratorsOfLinealitySpace(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->generatorsOfLinealitySpace();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getGeneratorsOfLinealitySpace: unexpected parameters");
  return TRUE;
}

BOOLEAN getRays(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->extremeRays();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getRays: unexpected parameters");
  return TRUE;
}

BOOLEAN getQuotientLatticeBasis(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->quotientLatticeBasis();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getQuotientLatticeBasis: unexpected parameters");
  return TRUE;
}

BOOLEAN getLinearForms(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZMatrix zmat = zc->getLinearForms();
    res->rtyp = INTMAT_CMD;
    res->data = (void*)zMatrix2Intvec(zmat);
    return FALSE;
  }
  WerrorS("getLinearForms: unexpected parameters");
  return TRUE;
}

BOOLEAN getAmbientDimension(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int i = zc->ambientDimension();
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("getLinearForms: unexpected parameters");
  return TRUE;
}

BOOLEAN getDimension(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int i = zc->dimension();
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("getDimension: unexpected parameters");
  return TRUE;
}

BOOLEAN getCodimension(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int i = zc->codimension();
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("getCodimension: unexpected parameters");
  return TRUE;
}

BOOLEAN getLinealityDimension(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int i = zc->dimensionOfLinealitySpace();
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("getLinealityDimension: unexpected parameters");
  return TRUE;
}

BOOLEAN getMultiplicity(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    bool ok = true;
    int i = integerToInt(zc->getMultiplicity(), ok);
    if (!ok)
      WerrorS("overflow while converting a gfan::Integer to an int");
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("getMultiplicity: unexpected parameters");
  return TRUE;
}

BOOLEAN isOrigin(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int i = zc->isOrigin() ? 1 : 0;
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("isOrigin: unexpected parameters");
  return TRUE;
}

BOOLEAN isFullSpace(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int i = zc->isFullSpace() ? 1 : 0;
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("isFullSpace: unexpected parameters");
  return TRUE;
}

BOOLEAN isSimplicial(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int i = zc->isSimplicial() ? 1 : 0;
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("isSimplicial: unexpected parameters");
  return TRUE;
}

BOOLEAN containsPositiveVector(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int i = zc->containsPositiveVector() ? 1 : 0;
    res->rtyp = INT_CMD;
    res->data = (void*) i;
    return FALSE;
  }
  WerrorS("containsPositiveVector: unexpected parameters");
  return TRUE;
}

BOOLEAN getLinealitySpace(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZCone* zd = &zc->linealitySpace();
    res->rtyp = coneID;
    res->data = (void*) zd;
    return FALSE;
  }
  WerrorS("getLinealitySpace: unexpected parameters");
  return TRUE;
}

BOOLEAN getDualCone(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZCone* zd = &zc->dualCone();
    res->rtyp = coneID;
    res->data = (void*) zd;
    return FALSE;
  }
  WerrorS("getDualCone: unexpected parameters");
  return TRUE;
}

BOOLEAN getNegated(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZCone* zd = &zc->negated();
    res->rtyp = coneID;
    res->data = (void*) zd;
    return FALSE;
  }
  WerrorS("getNegated: unexpected parameters");
  return TRUE;
}

BOOLEAN getSemigroupGenerator(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    int d = zc->dimension();
    int dLS = zc->dimensionOfLinealitySpace();
    if (d == dLS + 1)
    {
      gfan::ZVector zv = zc->semiGroupGeneratorOfRay();
      res->rtyp = INTVEC_CMD;
      res->data = (void*) zVector2Intvec(zv);
      return FALSE;
    }
    else
    {
      Werror("expected dim of cone one larger than dim of lin space\n"
             "but got dimensions %d and %d", d, dLS);
    }
  }
  WerrorS("getSemigroupGenerator: unexpected parameters");
  return TRUE;
}

BOOLEAN getRelativeInteriorPoint(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZVector zv = zc->getRelativeInteriorPoint();
    res->rtyp = INTVEC_CMD;
    res->data = (void*) zVector2Intvec(zv);
    return FALSE;
  }
  WerrorS("getRelativeInteriorPoint: unexpected parameters");
  return TRUE;
}

BOOLEAN getUniquePoint(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    gfan::ZVector zv = zc->getUniquePoint();
    res->rtyp = INTVEC_CMD;
    res->data = (void*) zVector2Intvec(zv);
    return FALSE;
  }
  WerrorS("getUniquePoint: unexpected parameters");
  return TRUE;
}

BOOLEAN setMultiplicity(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    leftv v = u->next;
    if ((v != NULL) && (v->Typ() == INT_CMD))
    {
      int val = (int)(long)v->Data();
      zc->setMultiplicity(gfan::Integer(val));
      res->rtyp = NONE;
      res->data = NULL;
      return FALSE;
    }
  }
  WerrorS("setMultiplicity: unexpected parameters");
  return TRUE;
}

BOOLEAN setLinearForms(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    gfan::ZCone* zc = (gfan::ZCone*)u->Data();
    leftv v = u->next;
    if ((v != NULL) && (v->Typ() == INTVEC_CMD))
    {
      intvec* mat = (intvec*)v->Data();
      gfan::ZMatrix zm = intmat2ZMatrix(mat);
      zc->setLinearForms(zm);
      res->rtyp = NONE;
      res->data = NULL;
      return FALSE;
    }
  }
  WerrorS("setLinearForms: unexpected parameters");
  return TRUE;
}

BOOLEAN cone_intersect(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    leftv v = u->next;
    if ((v != NULL) && (v->Typ() == coneID))
    {
      gfan::ZCone* zc1 = (gfan::ZCone*)u->Data();
      gfan::ZCone* zc2 = (gfan::ZCone*)v->Data();
      int d1 = zc1->ambientDimension();
      int d2 = zc2->ambientDimension();
      if (d1 != d2)
        Werror("expected ambient dims of both cones to coincide\n"
               "but got %d and %d", d1, d2);
      gfan::ZCone zc3 = gfan::intersection(*zc1, *zc2);
      zc3.canonicalize();
      res->rtyp = coneID;
      res->data = (void *)new gfan::ZCone(zc3);
      return FALSE;
    }
  }
  WerrorS("cone_intersect: unexpected parameters");
  return TRUE;
}

BOOLEAN cone_link(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    leftv v = u->next;
    if ((v != NULL) && (v->Typ() == INTVEC_CMD))
    {
      gfan::ZCone* zc = (gfan::ZCone*)u->Data();
      intvec* iv = (intvec*)v->Data();
      gfan::ZVector zv= intvec2ZVector(iv);
      int d1 = zc->ambientDimension();
      int d2 = zv.size();
      if (d1 != d2)
        Werror("expected ambient dim of cone and size of vector\n"
               "to be equal but got %d and %d", d1, d2);
      if(!zc->contains(zv))
      {
        WerrorS("the provided intvec does not lie in the cone");
      }
      res->rtyp = coneID;
      res->data = (void *)new gfan::ZCone(zc->link(zv));
      return FALSE;
    }
  }
  WerrorS("cone_link: unexpected parameters");
  return TRUE;
}

BOOLEAN contains(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    leftv v = u->next;
    if ((v != NULL) && (v->Typ() == coneID))
    {
      gfan::ZCone* zc = (gfan::ZCone*)u->Data();
      gfan::ZCone* zd = (gfan::ZCone*)v->Data();
      int d1 = zc->ambientDimension();
      int d2 = zd->ambientDimension();
      if (d1 == d2)
      {
        res->rtyp = INT_CMD;
        res->data = (void *)(zc->contains(*zd) ? 1 : 0);
        return FALSE;
      }
      Werror("expected cones with same ambient dimensions\n but got"
             " dimensions %d and %d", d1, d2);
    }
    if ((v != NULL) && (v->Typ() == INTVEC_CMD))
    {
      gfan::ZCone* zc = (gfan::ZCone*)u->Data();
      intvec* vec = (intvec*)v->Data();
      gfan::ZVector zv = intvec2ZVector(vec);
      int d1 = zc->ambientDimension();
      int d2 = zv.size();
      if (d1 == d2)
      {
        res->rtyp = INT_CMD;
        res->data = (void *)(zc->contains(zv) ? 1 : 0);
      }
      Werror("expected ambient dim of cone and size of vector\n"
             "to be equal but got %d and %d", d1, d2);
    }
  }
  WerrorS("containsCone: unexpected parameters");
  return TRUE;
}

BOOLEAN containsRelatively(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    leftv v = u->next;
    if ((v != NULL) && (v->Typ() == INTVEC_CMD))
    {
      gfan::ZCone* zc = (gfan::ZCone*)u->Data();
      intvec* vec = (intvec*)v->Data();
      gfan::ZVector zv = intvec2ZVector(vec);
      int d1 = zc->ambientDimension();
      int d2 = zv.size();
      if (d1 == d2)
      {
        res->rtyp = INT_CMD;
        res->data = (void *)(zc->containsRelatively(zv) ? 1 : 0);
      }
      Werror("expected ambient dim of cone and size of vector\n"
             "to be equal but got %d and %d", d1, d2);     
    }
  }
  WerrorS("containsCone: unexpected parameters");
  return TRUE;
}

BOOLEAN hasFace(leftv res, leftv args)
{
  leftv u = args;
  if ((u != NULL) && (u->Typ() == coneID))
  {
    leftv v = u->next;
    if ((v != NULL) && (v->Typ() == coneID))
    {
      gfan::ZCone* zc = (gfan::ZCone*)u->Data();
      gfan::ZCone* zf = (gfan::ZCone*)v->Data();
      int retInt;
      gfan::ZVector point = zf->getRelativeInteriorPoint();
      gfan::ZMatrix ineq = zc->getInequalities();
      if(zc->hasFace(*zf))
        {retInt = 1;}
      else
        {retInt = 0;}
      res->rtyp = INT_CMD;
      res->data = (char*) retInt;
      return FALSE;
    } 
    else
    {
      WerrorS("hasFace: unexpected parameters");
      return TRUE;
    }
  }
  else
  {
    WerrorS("hasFace: unexpected parameters");
    return TRUE;
  }
}

void bbcone_setup()
{
  blackbox *b=(blackbox*)omAlloc0(sizeof(blackbox));
  // all undefined entries will be set to default in setBlackboxStuff
  // the default Print is quite usefule,
  // all other are simply error messages
  b->blackbox_destroy=bbcone_destroy;
  b->blackbox_String=bbcone_String;
  //b->blackbox_Print=blackbox_default_Print;
  b->blackbox_Init=bbcone_Init;
  b->blackbox_Copy=bbcone_Copy;
  b->blackbox_Assign=bbcone_Assign;
  iiAddCproc("","cone_via_rays",FALSE,cone_via_rays);
  iiAddCproc("","cone_via_normals",FALSE,cone_via_normals);
  iiAddCproc("","cone_intersect",FALSE,cone_intersect);
  iiAddCproc("","cone_link",FALSE,cone_link);
  iiAddCproc("","contains",FALSE,contains);
  iiAddCproc("","getRays",FALSE,getRays);
  iiAddCproc("","getMultiplicity",FALSE,getMultiplicity);
  iiAddCproc("","setMultiplicty",FALSE,setMultiplicity);
  iiAddCproc("","getLinearForms",FALSE,getLinearForms);
  iiAddCproc("","setLinearForms",FALSE,setLinearForms);
  iiAddCproc("","getInequalities",FALSE,getInequalities);
  iiAddCproc("","getEquations",FALSE,getEquations);
  iiAddCproc("","getGeneratorsOfSpan",FALSE,getGeneratorsOfSpan);
  iiAddCproc("","getGeneratorsOfLinealitySpace",FALSE,getGeneratorsOfLinealitySpace);
  iiAddCproc("","getFacets",FALSE,getFacets);
  iiAddCproc("","getImpliedEquations",FALSE,getImpliedEquations);
  iiAddCproc("","getRelativeInteriorPoint",FALSE,getRelativeInteriorPoint);
  iiAddCproc("","getAmbientDimension",FALSE,getAmbientDimension);
  iiAddCproc("","getDimension",FALSE,getDimension);
  iiAddCproc("","getCodimension",FALSE,getCodimension);
  iiAddCproc("","getLinealityDimension",FALSE,getLinealityDimension);
  iiAddCproc("","isOrigin",FALSE,isOrigin);
  iiAddCproc("","isFullSpace",FALSE,isFullSpace);
  iiAddCproc("","isSimplicial",FALSE,isSimplicial);
  iiAddCproc("","containsPositiveVector",FALSE,containsPositiveVector);
  iiAddCproc("","getLinealitySpace",FALSE,getLinealitySpace);
  iiAddCproc("","getDualCone",FALSE,getDualCone);
  iiAddCproc("","getNegated",FALSE,getNegated);
  iiAddCproc("","getQuotientLatticeBasis",FALSE,getQuotientLatticeBasis);
  iiAddCproc("","getSemigroupGenerator",FALSE,getSemigroupGenerator);
  iiAddCproc("","getUniquePoint",FALSE,getUniquePoint);
  // iiAddCproc("","faceContaining",FALSE,faceContaining);
  iiAddCproc("","hasFace",FALSE,hasFace);
  coneID=setBlackboxStuff(b,"cone");
  //Print("created type %d (cone)\n",coneID); 
}

#endif
/* HAVE_FANS */
