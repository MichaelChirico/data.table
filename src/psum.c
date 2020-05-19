#include "data.table.h"

// like base::p{max,min}, but for sum
SEXP psum(SEXP x, SEXP narmArg) {
  if (!isNewList(x))
    error(_("Internal error: x must be a list")); // # nocov

  SEXP xj;
  int J=LENGTH(x);
  if (J == 0) {
    error(_("Empty input"));
  } else if (J == 1) {
    xj = VECTOR_ELT(x, 0);
    if (TYPEOF(xj) == VECSXP) { // e.g. psum(.SD)
      return psum(xj, narmArg);
    } else {
      // na.rm doesn't matter -- input --> output
      return xj;
    }
  }

  if (!isLogical(narmArg) || LENGTH(narmArg)!=1 || LOGICAL(narmArg)[0]==NA_LOGICAL)
    error(_("na.rm must be TRUE or FALSE"));

  SEXPTYPE outtype = INTSXP;
  int n = -1, nj, xi;
  for (int j=0; j<J; j++) {
    xj = VECTOR_ELT(x, j);
    switch(TYPEOF(xj)) {
    case LGLSXP: case INTSXP:
      if (isFactor(xj)) {
        error(_("%s not meaningful for factors"), "psum");
      }
      break;
    case REALSXP:
      if (INHERITS(xj, char_integer64)) {
        error(_("integer64 input not supported"));
      }
      if (outtype == INTSXP) { // bump if this is the first numeric we've seen
        outtype = REALSXP;
      }
      break;
    case CPLXSXP:
      if (outtype != CPLXSXP) { // only bump if we're not already complex
        outtype = CPLXSXP;
      }
      break;
    default:
      error(_("Only logical, numeric and complex inputs are supported for %s"), "psum");
    }
    if (n >= 0) {
      nj = LENGTH(xj);
      if (n == 1 && nj > 1) {
        n = nj; // singleton comes first, vector comes later [psum(1, 1:4)]
      } else if (nj != 1 && nj != n) {
        error(_("Inconsistent input lengths -- first found %d, but %d element has length %d. Only singletons will be recycled."), n, j+1, nj);
      }
    } else { // initialize
      n = LENGTH(xj);
    }
  }

  SEXP out = PROTECT(allocVector(outtype, n));
  if (n == 0) {
    UNPROTECT(1);
    return(out);
  }
  if (LOGICAL(narmArg)[0]) {
    // initialize to NA to facilitate all-NA rows --> NA output
    writeNA(out, 0, n);
    switch (outtype) {
    case INTSXP: {
      int *outp = INTEGER(out), *xjp;
      for (int j=0; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        xjp = INTEGER(xj); // INTEGER is the same as LOGICAL
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i; // recycling for singletons
          if (xjp[xi] != NA_INTEGER) { // NA_LOGICAL is the same
            if (outp[i] == NA_INTEGER) {
              outp[i] = xjp[xi];
            } else {
              if ((xjp[xi] > 0 && INT_MAX - xjp[xi] < outp[i]) ||
                  (xjp[xi] < 0 && INT_MIN - xjp[xi] > outp[i])) { // overflow
                error(_("Inputs have exceeded .Machine$integer.max=%d in absolute value; please cast to numeric first and try again"), INT_MAX);
              }
              outp[i] += xjp[xi];
            }
          } // else remain as NA
        }
      }
    } break;
    case REALSXP: { // REALSXP; special handling depending on whether each input is int/numeric
      double *outp = REAL(out), *xjp; // since outtype is REALSXP, there's at least one REAL column
      for (int j=0; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        switch (TYPEOF(xj)) {
        case LGLSXP: case INTSXP: {
          int *xjp = INTEGER(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            if (xjp[xi] != NA_INTEGER) {
              outp[i] = ISNAN(outp[i]) ? xjp[xi] : outp[i] + xjp[xi];
            }
          }
        } break;
        case REALSXP: {
          xjp = REAL(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            if (!ISNAN(xjp[xi])) {
              outp[i] = ISNAN(outp[i]) ? xjp[xi] : outp[i] + xjp[xi];
            }
          }
        } break;
        default:
          error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
        }
      }
    } break;
    case CPLXSXP: {
      Rcomplex *outp = COMPLEX(out), *xjp;
      for (int j=0; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        switch (TYPEOF(xj)) {
        case LGLSXP: case INTSXP: { // integer/numeric only increment the real part
          int *xjp = INTEGER(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            if (xjp[xi] != NA_INTEGER) {
              if (ISNAN_COMPLEX(outp[i])) {
                outp[i].r = xjp[xi];
                outp[i].i = 0;
              } else {
                outp[i].r += xjp[xi];
              }
            }
          }
        } break;
        case REALSXP: {
          double *xjp = REAL(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            if (!ISNAN(xjp[xi])) {
              if (ISNAN_COMPLEX(outp[i])) {
                outp[i].r = xjp[xi];
                outp[i].i = 0;
              } else {
                outp[i].r += xjp[xi];
              }
            }
          }
        } break;
        case CPLXSXP: {
          xjp = COMPLEX(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            // can construct complex vectors with !is.na(Re) & is.na(Im) --
            //   seems dubious to me, since print(z) only shows NA, not 3 + NAi
            if (!ISNAN_COMPLEX(xjp[xi])) {
              outp[i].r = ISNAN(outp[i].r) ? xjp[xi].r : outp[i].r + xjp[xi].r;
              outp[i].i = ISNAN(outp[i].i) ? xjp[xi].i : outp[i].i + xjp[xi].i;
            }
          }
        } break;
        default:
          error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
        }
      }
    } break;
    default:
      error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
    }
  } else { // na.rm=FALSE
    switch (outtype) {
    case INTSXP: {
      int *outp = INTEGER(out), *xjp;
      xj = VECTOR_ELT(x, 0);
      nj = LENGTH(xj);
      xjp = INTEGER(xj);
      for (int i=0; i<n; i++) outp[i] = xjp[nj == 1 ? 0 : i];
      for (int j=1; j<J; j++) { // J>=2 by now
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        xjp = INTEGER(xj);
        for (int i=0; i<n; i++) {
          if (outp[i] == NA_INTEGER) {
            continue;
          }
          xi = nj == 1 ? 0 : i;
          if (xjp[xi] == NA_INTEGER) {
            outp[i] = NA_INTEGER;
          } else if ((xjp[xi] > 0 && INT_MAX - xjp[xi] < outp[i]) ||
                     (xjp[xi] < 0 && INT_MIN - xjp[xi] > outp[i])) {
            warning(_("Inputs have exceeded .Machine$integer.max=%d in absolute value; returning NA. Please cast to numeric first to avoid this."), INT_MAX);
            outp[i] = NA_INTEGER;
          } else {
            outp[i] += xjp[xi];
          }
        }
      }
    } break;
    case REALSXP: {
      double *outp = REAL(out), *xjp;
      xj = VECTOR_ELT(x, 0);
      nj = LENGTH(xj);
      if (TYPEOF(xj) == REALSXP) {
        xjp = REAL(xj);
        for (int i=0; i<n; i++) outp[i] = xjp[nj == 1 ? 0 : i];
      } else {
        int *xjp = INTEGER(xj);
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i;
          outp[i] = xjp[xi] == NA_INTEGER ? NA_REAL : xjp[xi];
        }
      }
      for (int j=1; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        switch (TYPEOF(xj)) {
        case LGLSXP: case INTSXP: {
          int *xjp = INTEGER(xj);
          for (int i=0; i<n; i++) {
            if (ISNAN(outp[i])) {
              continue;
            }
            xi = nj == 1 ? 0 : i;
            outp[i] = xjp[xi] == NA_INTEGER ? NA_REAL : outp[i] + xjp[xi];
          }
        } break;
        case REALSXP: {
          xjp = REAL(xj);
          for (int i=0; i<n; i++) {
            if (ISNAN(outp[i])) {
              continue;
            }
            xi = nj == 1 ? 0 : i;
            outp[i] = ISNAN(xjp[xi]) ? NA_REAL : outp[i] + xjp[xi];
          }
        } break;
        default:
          error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
        }
      }
    } break;
    case CPLXSXP: {
      Rcomplex *outp = COMPLEX(out), *xjp;
      xj = VECTOR_ELT(x, 0);
      nj = LENGTH(xj);
      switch (TYPEOF(xj)) {
      case LGLSXP: case INTSXP: {
        int *xjp = INTEGER(xj);
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i;
          outp[i].r = xjp[xi] == NA_INTEGER ? NA_REAL : xjp[xi];
          outp[i].i = xjp[xi] == NA_INTEGER ? NA_REAL : 0;
        }
      } break;
      case REALSXP: {
        double *xjp = REAL(xj);
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i;
          outp[i].r = xjp[xi];
          outp[i].i = ISNAN(xjp[xi]) ? NA_REAL : 0;
        }
      } break;
      case CPLXSXP: {
        xjp = COMPLEX(xj);
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i;
          outp[i].r = xjp[xi].r;
          outp[i].i = xjp[xi].i;
        }
      } break;
      default:
        error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
      }
      for (int j=1; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        switch (TYPEOF(xj)) {
        case LGLSXP: case INTSXP: {
          int *xjp = INTEGER(xj);
          for (int i=0; i<n; i++) {
            if (!ISNAN_COMPLEX(outp[i])) {
              xi = nj == 1 ? 0 : i;
              if (xjp[xi] == NA_INTEGER) {
                outp[i].r = NA_REAL;
                outp[i].i = NA_REAL;
              } else {
                outp[i].r += xjp[xi];
              }
            }
          }
        } break;
        case REALSXP: {
          double *xjp = REAL(xj);
          for (int i=0; i<n; i++) {
            if (!ISNAN_COMPLEX(outp[i])) {
              xi = nj == 1 ? 0 : i;
              if (ISNAN(xjp[xi])) {
                outp[i].r = NA_REAL;
                outp[i].i = NA_REAL;
              } else {
                outp[i].r += xjp[xi];
              }
            }
          }
        } break;
        case CPLXSXP: {
          xjp = COMPLEX(xj);
          for (int i=0; i<n; i++) {
            if (!ISNAN_COMPLEX(outp[i])) {
              xi = nj == 1 ? 0 : i;
              if (ISNAN_COMPLEX(xjp[xi])) {
                outp[i].r = NA_REAL;
                outp[i].i = NA_REAL;
              } else {
                outp[i].r += xjp[xi].r;
                outp[i].i += xjp[xi].i;
              }
            }
          }
        } break;
        default:
          error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
        }
      }
    } break;
    default:
      error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
    }
  }

  UNPROTECT(1);
  return out;
}

SEXP pprod(SEXP x, SEXP narmArg) {
  if (!isNewList(x))
    error(_("Internal error: x must be a list")); // # nocov

  SEXP xj;
  int J=LENGTH(x);
  if (J == 0) {
    error(_("Empty input"));
  } else if (J == 1) {
    SEXP xj = VECTOR_ELT(x, 0);
    if (TYPEOF(xj) == VECSXP) {
      return pprod(xj, narmArg);
    } else {
      // na.rm doesn't matter -- input --> output
      return xj;
    }
  }

  if (!isLogical(narmArg) || LENGTH(narmArg)!=1 || LOGICAL(narmArg)[0]==NA_LOGICAL)
    error(_("na.rm must be TRUE or FALSE"));

  SEXPTYPE outtype = INTSXP;
  int n = -1, nj, xi;
  for (int j=0; j<J; j++) {
    xj = VECTOR_ELT(x, j);
    switch(TYPEOF(xj)) {
    case LGLSXP: case INTSXP:
      if (isFactor(xj)) {
        error(_("%s not meaningful for factors"), "pprod");
      }
      break;
    case REALSXP:
      if (INHERITS(xj, char_integer64)) {
        error(_("integer64 input not supported"));
      }
      if (outtype == INTSXP) { // bump if this is the first numeric we've seen
        outtype = REALSXP;
      }
      break;
    case CPLXSXP:
      if (outtype != CPLXSXP) { // only bump if we're not already complex
        outtype = CPLXSXP;
      }
      break;
    default:
      error(_("Only logical, numeric and complex inputs are supported for %s"), "pprod");
    }
    if (n >= 0) {
      nj = LENGTH(xj);
      if (n == 1 && nj > 1) {
        n = nj; // singleton comes first, vector comes later [pprod(1, 1:4)]
      } else if (nj != 1 && nj != n) {
        error(_("Inconsistent input lengths -- first found %d, but %d element has length %d. Only singletons will be recycled."), n, j+1, nj);
      }
    } else { // initialize
      n = LENGTH(xj);
    }
  }

  SEXP out = PROTECT(allocVector(outtype, n));
  if (n == 0) {
    UNPROTECT(1);
    return(out);
  }
  if (LOGICAL(narmArg)[0]) {
    // initialize to NA to facilitate all-NA rows --> NA output
    writeNA(out, 0, n);
    switch (outtype) {
    case INTSXP: {
      int *outp = INTEGER(out), *xjp;
      for (int j=0; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        xjp = INTEGER(xj); // INTEGER is the same as LOGICAL
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i; // recycling for singletons
          if (xjp[xi] != NA_INTEGER) { // NA_LOGICAL is the same
            if (outp[i] == NA_INTEGER) {
              outp[i] = xjp[xi];
            } else {
              if ((outp[i] > 0 && (xjp[xi] > INT_MAX/outp[i] || xjp[xi] < INT_MIN/outp[i])) || // overflow -- be careful of inequalities and flipping signs
                  (outp[i] == -1 && (xjp[xi] > INT_MAX || xjp[xi] <= INT_MIN)) ||              // ASSUMPTION: INT_MIN= -INT_MAX - 1
                  (outp[i] < -1 && (xjp[xi] < INT_MAX/outp[i] || xjp[xi] > INT_MIN/outp[i]))) {
                error(_("Inputs have exceeded .Machine$integer.max=%d in absolute value; please cast to numeric first and try again"), INT_MAX);
              }
              outp[i] *= xjp[xi];
            }
          } // else remain as NA
        }
      }
    } break;
    case REALSXP: { // REALSXP; special handling depending on whether each input is int/numeric
      double *outp = REAL(out), *xjp;
      for (int j=0; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        switch (TYPEOF(xj)) {
        case LGLSXP: case INTSXP: {
          int *xjp = INTEGER(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            if (xjp[xi] != NA_INTEGER) {
              outp[i] = ISNAN(outp[i]) ? xjp[xi] : outp[i] * xjp[xi];
            }
          }
        } break;
        case REALSXP: {
          xjp = REAL(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            if (!ISNAN(xjp[xi])) {
              outp[i] = ISNAN(outp[i]) ? xjp[xi] : outp[i] * xjp[xi];
            }
          }
        } break;
        default:
          error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
        }
      }
    } break;
    case CPLXSXP: {
      Rcomplex *outp = COMPLEX(out), *xjp;
      for (int j=0; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        switch (TYPEOF(xj)) {
        case LGLSXP: case INTSXP: { // integer/numeric only increment the real part
          int *xjp = INTEGER(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            if (xjp[xi] != NA_INTEGER) {
              if (ISNAN_COMPLEX(outp[i])) {
                outp[i].r = xjp[xi];
                outp[i].i = 0;
              } else {
                outp[i].r *= xjp[xi];
                outp[i].i *= xjp[xi];
              }
            }
          }
        } break;
        case REALSXP: {
          double *xjp = REAL(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            if (!ISNAN(xjp[xi])) {
              if (ISNAN_COMPLEX(outp[i])) {
                outp[i].r = xjp[xi];
                outp[i].i = 0;
              } else {
                outp[i].r *= xjp[xi];
                outp[i].i *= xjp[xi];
              }
            }
          }
        } break;
        case CPLXSXP: {
          xjp = COMPLEX(xj);
          for (int i=0; i<n; i++) {
            xi = nj == 1 ? 0 : i;
            // can construct complex vectors with !is.na(Re) & is.na(Im) --
            //   seems dubious to me, since print(z) only shows NA, not 3 + NAi
            if (!ISNAN_COMPLEX(xjp[xi])) {
              if (ISNAN(outp[i].r) || ISNAN(outp[i].i)) {
                outp[i].r = xjp[xi].r;
                outp[i].i = xjp[xi].i;
              } else {
                double tmp=outp[i].r; // can't simultaneously assign Re&Im, need to remember Re pre-update
                outp[i].r = outp[i].r * xjp[xi].r - outp[i].i * xjp[xi].i;
                outp[i].i = outp[i].i * xjp[xi].r + tmp * xjp[xi].i;
              }
            }
          }
        } break;
        default:
          error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
        }
      }
    } break;
    default:
      error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
    }
  } else { // na.rm=FALSE
    switch (outtype) {
    case INTSXP: {
      int *outp = INTEGER(out), *xjp;
      xj = VECTOR_ELT(x, 0);
      nj = LENGTH(xj);
      xjp = INTEGER(xj);
      for (int i=0; i<n; i++) outp[i] = xjp[nj == 1 ? 0 : i];
      for (int j=1; j<J; j++) { // J>=2 by now
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        xjp = INTEGER(xj);
        for (int i=0; i<n; i++) {
          if (outp[i] == NA_INTEGER) {
            continue;
          }
          xi = nj == 1 ? 0 : i;
          if (xjp[xi] == NA_INTEGER) {
            outp[i] = NA_INTEGER;
          } else if (outp[i] == 0) {
            continue; // 0 is a steady state, except when xjp[xi] is missing
          } else {
            if ((outp[i] > 0 && (xjp[xi] > INT_MAX/outp[i] || xjp[xi] < INT_MIN/outp[i])) || // overflow -- be careful of inequalities and flipping signs
                (outp[i] == -1 && (xjp[xi] > INT_MAX || xjp[xi] <= INT_MIN)) ||              // ASSUMPTION: INT_MIN= -INT_MAX - 1
                (outp[i] < -1 && (xjp[xi] < INT_MAX/outp[i] || xjp[xi] > INT_MIN/outp[i]))) {
              warning(_("Inputs have exceeded .Machine$integer.max=%d in absolute value; returning NA. Please cast to numeric first to avoid this."), INT_MAX);
              outp[i] = NA_INTEGER;
            } else {
              outp[i] *= xjp[xi];
            }
          }
        }
      }
    } break;
    case REALSXP: {
      double *outp = REAL(out), *xjp;
      xj = VECTOR_ELT(x, 0);
      nj = LENGTH(xj);
      if (TYPEOF(xj) == REALSXP) {
        xjp = REAL(xj);
        for (int i=0; i<n; i++) outp[i] = xjp[nj == 1 ? 0 : i];
      } else {
        int *xjp = INTEGER(xj);
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i;
          outp[i] = xjp[xi] == NA_INTEGER ? NA_REAL : xjp[xi];
        }
      }
      for (int j=1; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        switch (TYPEOF(xj)) {
        case LGLSXP: case INTSXP: {
          int *xjp = INTEGER(xj);
          for (int i=0; i<n; i++) {
            if (ISNAN(outp[i])) {
              continue;
            }
            xi = nj == 1 ? 0 : i;
            if (xjp[xi] == NA_INTEGER) {
              outp[i] = NA_REAL;
            } else if (outp[i] == 0) {
              continue;
            } else {
              outp[i] *= xjp[xi];
            }
          }
        } break;
        case REALSXP: {
          xjp = REAL(xj);
          for (int i=0; i<n; i++) {
            if (ISNAN(outp[i])) {
              continue;
            }
            xi = nj == 1 ? 0 : i;
            if (ISNAN(xjp[xi])) {
              outp[i] = NA_REAL;
            } else if (outp[i] == 0) {
              continue;
            } else {
              outp[i] *= xjp[xi];
            }
          }
        } break;
        default:
          error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
        }
      }
    } break;
    case CPLXSXP: {
      Rcomplex *outp = COMPLEX(out), *xjp;
      xj = VECTOR_ELT(x, 0);
      nj = LENGTH(xj);
      switch (TYPEOF(xj)) {
      case LGLSXP: case INTSXP: {
        int *xjp = INTEGER(xj);
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i;
          outp[i].r = xjp[xi];
          outp[i].i = xjp[xi] == NA_INTEGER ? NA_REAL : 0;
        }
      } break;
      case REALSXP: {
        double *xjp = REAL(xj);
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i;
          outp[i].r = xjp[xi];
          outp[i].i = ISNAN(xjp[xi]) ? NA_REAL : 0;
        }
      } break;
      case CPLXSXP: {
        xjp = COMPLEX(xj);
        for (int i=0; i<n; i++) {
          xi = nj == 1 ? 0 : i;
          outp[i].r = xjp[xi].r;
          outp[i].i = xjp[xi].i;
        }
      } break;
      default:
        error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
      }
      for (int j=1; j<J; j++) {
        xj = VECTOR_ELT(x, j);
        nj = LENGTH(xj);
        switch (TYPEOF(xj)) {
        case LGLSXP: case INTSXP: {
          int *xjp = INTEGER(xj);
          for (int i=0; i<n; i++) {
            if (!ISNAN_COMPLEX(outp[i])) {
              xi = nj == 1 ? 0 : i;
              if (xjp[xi] == NA_INTEGER) {
                outp[i].r = NA_REAL;
                outp[i].i = NA_REAL;
              } else if (outp[i].r == 0 && outp[i].i == 0) {
                continue;
              } else {
                outp[i].r *= xjp[xi];
                outp[i].i *= xjp[xi];
              }
            }
          }
        } break;
        case REALSXP: {
          double *xjp = REAL(xj);
          for (int i=0; i<n; i++) {
            if (!ISNAN_COMPLEX(outp[i])) {
              xi = nj == 1 ? 0 : i;
              if (ISNAN(xjp[xi])) {
                outp[i].r = NA_REAL;
                outp[i].i = NA_REAL;
              } else if (outp[i].r == 0 && outp[i].i == 0) {
                continue;
              } else {
                outp[i].r *= xjp[xi];
                outp[i].i *= xjp[xi];
              }
            }
          }
        } break;
        case CPLXSXP: {
          xjp = COMPLEX(xj);
          for (int i=0; i<n; i++) {
            if (!ISNAN_COMPLEX(outp[i])) {
              xi = nj == 1 ? 0 : i;
              if (ISNAN_COMPLEX(xjp[xi])) {
                outp[i].r = NA_REAL;
                outp[i].i = NA_REAL;
              } else if (outp[i].r == 0 && outp[i].i == 0) {
                continue;
              } else {
                double tmp=outp[i].r; // see na.rm=TRUE branch
                outp[i].r = outp[i].r * xjp[xi].r - outp[i].i * xjp[xi].i;
                outp[i].i = tmp * xjp[xi].i + outp[i].i * xjp[xi].r;
              }
            }
          }
        } break;
        default:
          error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
        }
      }
    } break;
    default:
      error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
    }
  }

  UNPROTECT(1);
  return out;
}

SEXP pany(SEXP x, SEXP narmArg) {
  if (!isNewList(x))
    error(_("Internal error: x must be a list")); // # nocov

  SEXP xj, out;
  int J=LENGTH(x);
  if (J == 0) {
    error(_("Empty input"));
  } else if (J == 1) {
    xj = VECTOR_ELT(x, 0);
    if (TYPEOF(xj) == VECSXP) {
      return pany(xj, narmArg);
    } else {
      // maybe need to do coercion to logical
      if (TYPEOF(xj) == LGLSXP) {
        return xj;
      } else {
        out = PROTECT(coerceVector(xj, LGLSXP));
        UNPROTECT(1);
        return out;
      }
    }
  }

  if (!isLogical(narmArg) || LENGTH(narmArg)!=1 || LOGICAL(narmArg)[0]==NA_LOGICAL)
    error(_("na.rm must be TRUE or FALSE"));

  int n = -1, nj, xi;
  for (int j=0; j<J; j++) {
    xj = VECTOR_ELT(x, j);
    switch(TYPEOF(xj)) {
    case LGLSXP: case INTSXP:
      if (isFactor(xj)) {
        error(_("%s not meaningful for factors"), "pany");
      }
      break;
    case REALSXP:
      if (INHERITS(xj, char_integer64)) {
        error(_("integer64 input not supported"));
      }
      break;
    case CPLXSXP:
      break;
    default:
      error(_("Only logical, numeric and complex inputs are supported for %s"), "pany");
    }
    if (n >= 0) {
      nj = LENGTH(xj);
      if (n == 1 && nj > 1) {
        n = nj; // singleton comes first, vector comes later [psum(1, 1:4)]
      } else if (nj != 1 && nj != n) {
        error(_("Inconsistent input lengths -- first found %d, but %d element has length %d. Only singletons will be recycled."), n, j+1, nj);
      }
    } else { // initialize
      n = LENGTH(xj);
    }
  }

  out = PROTECT(allocVector(LGLSXP, n));
  int *outp = LOGICAL(out);
  if (n == 0) {
    UNPROTECT(1);
    return(out);
  }
  if (LOGICAL(narmArg)[0]) {
    // initialize to NA to facilitate all-NA rows --> NA output
    writeNA(out, 0, n);
    /* Logic table for any, na.rm=TRUE. > -_> do nothing; @b --> update to b
     *    | x: ||  0 |  1 | NA |
     *    +----||----|----|----|
     *  o |  0 ||  > | @1 |  > |
     *  u |  1 ||  > |  > |  > |
     *  t | NA || @0 | @1 |  > |
     */
    for (int j=0; j<J; j++) {
      xj = VECTOR_ELT(x, j);
      nj = LENGTH(xj);
      switch (TYPEOF(xj)) {
      case LGLSXP: case INTSXP: {
        int *xjp = INTEGER(xj);
        // for any, if there's a scalar 1, we're done, otherwise, skip
        if (nj == 1) {
          if (xjp[0] == NA_INTEGER)
            continue;
          if (xjp[0] != 0) {
            for (int i=0; i<n; i++) outp[i] = 1;
            break;
          }
          for (int i=0; i<n; i++) {
            if (outp[i] == NA_INTEGER) {
              outp[i] = 0;
            }
          }
          continue;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 1 || xjp[i] == NA_INTEGER)
            continue;
          if (xjp[i] != 0) {
            outp[i] = 1;
            continue;
          }
          // besides maybe in edge cases, it's faster to just
          //   write 0 than branch on outp[i]==NA
          outp[i] = 0;
        }
      } break;
      case REALSXP: {
        double *xjp = REAL(xj);
        if (nj == 1) {
          if (ISNAN(xjp[0]))
            continue;
          if (xjp[0] != 0) {
            for (int i=0; i<n; i++) outp[i] = 1;
            break;
          }
          for (int i=0; i<n; i++) {
            if (outp[i] == NA_INTEGER) {
              outp[i] = 0;
            }
          }
          continue;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 1 || ISNAN(xjp[i]))
            continue;
          if (xjp[i] != 0) {
            outp[i] = 1;
            continue;
          }
          outp[i] = 0;
        }
      } break;
      case CPLXSXP: {
        Rcomplex *xjp = COMPLEX(xj);
        if (nj == 1) {
          if (ISNAN_COMPLEX(xjp[0]))
            continue;
          if (xjp[0].r != 0 || xjp[0].i != 0) {
            for (int i=0; i<n; i++) outp[i] = 1;
            break;
          }
          for (int i=0; i<n; i++) {
            if (outp[i] == NA_INTEGER) {
              outp[i] = 0;
            }
          }
          continue;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 1 || ISNAN_COMPLEX(xjp[i]))
            continue;
          if (xjp[i].r != 0 || xjp[i].i != 0) {
            outp[i] = 1;
            continue;
          }
          outp[i] = 0;
        }
      } break;
      default:
        error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
      }
    }
  } else { // na.rm=FALSE
    /* Logic table for any, na.rm=FALSE. > -_> do nothing; @b --> update to b
     *    | x: || 0 |  1 |  NA |
     *    +----||---|----|-----|
     *  o |  0 || > | @1 | @NA |
     *  u |  1 || > |  > |   > |
     *  t | NA || > | @1 |   > |
     */
    xj = VECTOR_ELT(x, 0);
    nj = LENGTH(xj);
    switch (TYPEOF(xj)) {
    case LGLSXP: case INTSXP: {
      int *xjp = INTEGER(xj);
      for (int i=0; i<n; i++) {
        xi = nj == 1 ? 0 : i;
        outp[i] = xjp[xi] == 0 ? 0 : (xjp[xi] == NA_INTEGER ? NA_LOGICAL : 1);
      }
    } break;
    case REALSXP: {
      double *xjp = REAL(xj);
      for (int i=0; i<n; i++) {
        xi = nj == 1 ? 0 : i;
        outp[i] = xjp[xi] == 0 ? 0 : (ISNAN(xjp[xi]) ? NA_LOGICAL : 1);
      }
    } break;
    case CPLXSXP: {
      Rcomplex *xjp = COMPLEX(xj);
      for (int i=0; i<n; i++) {
        xi = nj == 1 ? 0 : i;
        outp[i] = xjp[xi].r == 0 && xjp[xi].i == 0 ? 0 :
          (ISNAN(xjp[xi].r) || ISNAN(xjp[xi].i) ? NA_LOGICAL : 1);
      }
    } break;
    default:
      error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
    }
    for (int j=1; j<J; j++) {
      xj = VECTOR_ELT(x, j);
      nj = LENGTH(xj);
      switch (TYPEOF(xj)) {
      case LGLSXP: case INTSXP: {
        int *xjp = INTEGER(xj);
        if (nj == 1) {
          if (xjp[0] == 0)
            continue;
          if (xjp[0] == NA_INTEGER) {
            for (int i=0; i<n; i++) {
              if (outp[i] == 0) {
                outp[i] = NA_INTEGER;
              }
            }
            continue;
          }
          // xjp[0] is 1
          for (int i=0; i<n; i++) outp[i] = 1;
          break;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 1 || xjp[i] == 0)
            continue;
          if (xjp[i] == NA_INTEGER) {
            // write even if it's already NA_INTEGER
            outp[i] = NA_INTEGER;
            continue;
          }
          outp[i] = 1;
        }
      } break;
      case REALSXP: {
        double *xjp = REAL(xj);
        if (nj == 1) {
          if (xjp[0] == 0)
            continue;
          if (ISNAN(xjp[0])) {
            for (int i=0; i<n; i++) {
              if (outp[i] == 0) {
                outp[i] = NA_INTEGER;
              }
            }
            continue;
          }
          // xjp[0] is non-zero, non-NA
          for (int i=0; i<n; i++) outp[i] = 1;
          break;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 1 || xjp[i] == 0)
            continue;
          if (ISNAN(xjp[i])) {
            // write even if it's already NA_INTEGER
            outp[i] = NA_INTEGER;
            continue;
          }
          outp[i] = 1;
        }
      } break;
      case CPLXSXP: {
        Rcomplex *xjp = COMPLEX(xj);
        if (nj == 1) {
          if (xjp[0].r == 0 && xjp[0].i == 0)
            continue;
          if (ISNAN_COMPLEX(xjp[0])) {
            for (int i=0; i<n; i++) {
              if (outp[i] == 0) {
                outp[i] = NA_INTEGER;
              }
            }
            continue;
          }
          // xjp[0] is non-zero, non-NA
          for (int i=0; i<n; i++) outp[i] = 1;
          break;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 1 || (xjp[i].r == 0 && xjp[i].i == 0))
            continue;
          if (ISNAN_COMPLEX(xjp[i])) {
            // write even if it's already NA_INTEGER
            outp[i] = NA_INTEGER;
            continue;
          }
          outp[i] = 1;
        }
      } break;
      default:
        error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
      }
    }
  }

  UNPROTECT(1);
  return out;
}

SEXP pall(SEXP x, SEXP narmArg) {
  if (!isNewList(x))
    error(_("Internal error: x must be a list")); // # nocov

  SEXP xj, out;
  int J=LENGTH(x);
  if (J == 0) {
    error(_("Empty input"));
  } else if (J == 1) {
    xj = VECTOR_ELT(x, 0);
    if (TYPEOF(xj) == VECSXP) {
      return pall(xj, narmArg);
    } else {
      // maybe need to do coercion to logical
      if (TYPEOF(xj) == LGLSXP) {
        return xj;
      } else {
        out = PROTECT(coerceVector(xj, LGLSXP));
        UNPROTECT(1);
        return out;
      }
    }
  }

  if (!isLogical(narmArg) || LENGTH(narmArg)!=1 || LOGICAL(narmArg)[0]==NA_LOGICAL)
    error(_("na.rm must be TRUE or FALSE"));

  int n = -1, nj, xi;
  for (int j=0; j<J; j++) {
    xj = VECTOR_ELT(x, j);
    switch(TYPEOF(xj)) {
    case LGLSXP: case INTSXP:
      if (isFactor(xj)) {
        error(_("%s not meaningful for factors"), "pall");
      }
      break;
    case REALSXP:
      if (INHERITS(xj, char_integer64)) {
        error(_("integer64 input not supported"));
      }
      break;
    case CPLXSXP:
      break;
    default:
      error(_("Only logical, numeric and complex inputs are supported for %s"), "pall");
    }
    if (n >= 0) {
      nj = LENGTH(xj);
      if (n == 1 && nj > 1) {
        n = nj; // singleton comes first, vector comes later [psum(1, 1:4)]
      } else if (nj != 1 && nj != n) {
        error(_("Inconsistent input lengths -- first found %d, but %d element has length %d. Only singletons will be recycled."), n, j+1, nj);
      }
    } else { // initialize
      n = LENGTH(xj);
    }
  }

  out = PROTECT(allocVector(LGLSXP, n));
  int *outp = LOGICAL(out);
  if (n == 0) {
    UNPROTECT(1);
    return(out);
  }
  if (LOGICAL(narmArg)[0]) {
    // initialize to NA to facilitate all-NA rows --> NA output
    writeNA(out, 0, n);
    /* Logic table for all, na.rm=TRUE. > -_> do nothing; @b --> update to b
     *    | x: ||  0 |  1 | NA |
     *    +----||----|----|----|
     *  o |  0 ||  > |  > |  > |
     *  u |  1 || @0 |  > |  > |
     *  t | NA || @0 | @1 |  > |
     */
    for (int j=0; j<J; j++) {
      xj = VECTOR_ELT(x, j);
      nj = LENGTH(xj);
      switch (TYPEOF(xj)) {
      case LGLSXP: case INTSXP: {
        int *xjp = INTEGER(xj);
        // for all, if there's a scalar 0, we're done, otherwise, skip
        if (nj == 1) {
          if (xjp[0] == NA_INTEGER)
            continue;
          if (xjp[0] == 0) {
            for (int i=0; i<n; i++) outp[i] = 0;
            break;
          }
          for (int i=0; i<n; i++) {
            if (outp[i] == NA_INTEGER) {
              outp[i] = 1;
            }
          }
          continue;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 0 || xjp[i] == NA_INTEGER)
            continue;
          if (xjp[i] == 0) {
            outp[i] = 0;
            continue;
          }
          // besides maybe in edge cases, it's faster to just
          //   write 1 than branch on outp[i]==NA
          outp[i] = 1;
        }
      } break;
      case REALSXP: {
        double *xjp = REAL(xj);
        if (nj == 1) {
          if (ISNAN(xjp[0]))
            continue;
          if (xjp[0] == 0) {
            for (int i=0; i<n; i++) outp[i] = 0;
            break;
          }
          for (int i=0; i<n; i++) {
            if (outp[i] == NA_INTEGER) {
              outp[i] = 1;
            }
            continue;
          }
          continue;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 0 || ISNAN(xjp[i]))
            continue;
          if (xjp[i] == 0) {
            outp[i] = 0;
            continue;
          }
          outp[i] = 1;
        }
      } break;
      case CPLXSXP: {
        Rcomplex *xjp = COMPLEX(xj);
        if (nj == 1) {
          if (ISNAN_COMPLEX(xjp[0]))
            continue;
          if (xjp[0].r == 0 && xjp[0].i == 0) {
            for (int i=0; i<n; i++) outp[i] = 0;
            break;
          }
          for (int i=0; i<n; i++) {
            if (outp[i] == NA_INTEGER) {
              outp[i] = 1;
            }
            continue;
          }
          continue;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 0 || ISNAN_COMPLEX(xjp[i]))
            continue;
          if (xjp[i].r == 0 && xjp[i].i == 0) {
            outp[i] = 0;
            continue;
          }
          outp[i] = 1;
        }
      } break;
      default:
        error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
      }
    }
  } else { // na.rm=FALSE
    /* Logic table for all, na.rm=FALSE. > -_> do nothing; @b --> update to b
     *    | x: ||  0 | 1 |  NA |
     *    +----||----|---|-----|
     *  o |  0 ||  > | > |   > |
     *  u |  1 || @0 | > | @NA |
     *  t | NA || @0 | > |   > |
     */
    xj = VECTOR_ELT(x, 0);
    nj = LENGTH(xj);
    switch (TYPEOF(xj)) {
    case LGLSXP: case INTSXP: {
      int *xjp = INTEGER(xj);
      for (int i=0; i<n; i++) {
        xi = nj == 1 ? 0 : i;
        outp[i] = xjp[xi] == 0 ? 0 : (xjp[xi] == NA_INTEGER ? NA_LOGICAL : 1);
      }
    } break;
    case REALSXP: {
      double *xjp = REAL(xj);
      for (int i=0; i<n; i++) {
        xi = nj == 1 ? 0 : i;
        outp[i] = xjp[xi] == 0 ? 0 : (ISNAN(xjp[xi]) ? NA_LOGICAL : 1);
      }
    } break;
    case CPLXSXP: {
      Rcomplex *xjp = COMPLEX(xj);
      for (int i=0; i<n; i++) {
        xi = nj == 1 ? 0 : i;
        outp[i] = xjp[xi].r == 0 && xjp[xi].i == 0 ? 0 :
          (ISNAN(xjp[xi].r) || ISNAN(xjp[xi].i) ? NA_LOGICAL : 1);
      }
    } break;
    default:
      error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
    }
    for (int j=1; j<J; j++) {
      xj = VECTOR_ELT(x, j);
      nj = LENGTH(xj);
      switch (TYPEOF(xj)) {
      case LGLSXP: case INTSXP: {
        int *xjp = INTEGER(xj);
        if (nj == 1) {
          if (xjp[0] == NA_INTEGER) {
            for (int i=0; i<n; i++) {
              if (outp[i] != 0) {
                outp[i] = NA_LOGICAL;
              }
            }
            continue;
          }
          if (xjp[0] != 0)
            continue;
          // xjp[0] is 0
          for (int i=0; i<n; i++) outp[i] = 0;
          break;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 0)
            continue;
          if (xjp[i] == 0) {
            outp[i] = 0;
            continue;
          }
          if (xjp[i] == NA_INTEGER) {
            // write even if it's already NA_INTEGER
            outp[i] = NA_INTEGER;
            continue;
          }
        }
      } break;
      case REALSXP: {
        double *xjp = REAL(xj);
        if (nj == 1) {
          if (ISNAN(xjp[0])) {
            for (int i=0; i<n; i++) {
              if (outp[i] != 0) {
                outp[i] = NA_LOGICAL;
              }
            }
            continue;
          }
          if (xjp[0] != 0)
            continue;
          // xjp[0] is 0
          for (int i=0; i<n; i++) outp[i] = 0;
          break;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 0)
            continue;
          if (xjp[i] == 0) {
            outp[i] = 0;
            continue;
          }
          if (ISNAN(xjp[i])) {
            // write even if it's already NA_INTEGER
            outp[i] = NA_INTEGER;
            continue;
          }
        }
      } break;
      case CPLXSXP: {
        Rcomplex *xjp = COMPLEX(xj);
        if (nj == 1) {
          if (ISNAN_COMPLEX(xjp[0])) {
            for (int i=0; i<n; i++) {
              if (outp[i] != 0) {
                outp[i] = NA_LOGICAL;
              }
            }
            continue;
          }
          if (xjp[0].r != 0 || xjp[0].i != 0)
            continue;
          // xjp[0] is 0
          for (int i=0; i<n; i++) outp[i] = 0;
          break;
        }
        for (int i=0; i<n; i++) {
          if (outp[i] == 0)
            continue;
          if (xjp[i].r == 0 && xjp[i].i == 0) {
            outp[i] = 0;
            continue;
          }
          if (ISNAN_COMPLEX(xjp[i])) {
            // write even if it's already NA_INTEGER
            outp[i] = NA_INTEGER;
            continue;
          }
        }
      } break;
      default:
        error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
      }
    }
  }

  UNPROTECT(1);
  return out;
}

SEXP ptest(SEXP x, SEXP narmArg) {
  if (!isNewList(x))
    error(_("Internal error: x must be a list")); // # nocov

  SEXP xj;
  int J=LENGTH(x);
  if (J == 0) {
    error(_("Empty input"));
  } else if (J == 1) {
    xj = VECTOR_ELT(x, 0);
    if (TYPEOF(xj) == VECSXP) { // e.g. psum(.SD)
      return psum(xj, narmArg);
    } else {
      // na.rm doesn't matter -- input --> output
      return xj;
    }
  }

  if (!isLogical(narmArg) || LENGTH(narmArg)!=1 || LOGICAL(narmArg)[0]==NA_LOGICAL)
    error(_("na.rm must be TRUE or FALSE"));

  SEXPTYPE outtype = INTSXP;
  int n = -1, nj, xi;
  for (int j=0; j<J; j++) {
    xj = VECTOR_ELT(x, j);
    switch(TYPEOF(xj)) {
    case LGLSXP: case INTSXP:
      if (isFactor(xj)) {
        error(_("%s not meaningful for factors"), "psum");
      }
      break;
    case REALSXP:
      if (INHERITS(xj, char_integer64)) {
        error(_("integer64 input not supported"));
      }
      if (outtype == INTSXP) { // bump if this is the first numeric we've seen
        outtype = REALSXP;
      }
      break;
    case CPLXSXP:
      if (outtype != CPLXSXP) { // only bump if we're not already complex
        outtype = CPLXSXP;
      }
      break;
    default:
      error(_("Only logical, numeric and complex inputs are supported for %s"), "psum");
    }
    if (n >= 0) {
      nj = LENGTH(xj);
      if (n == 1 && nj > 1) {
        n = nj; // singleton comes first, vector comes later [psum(1, 1:4)]
      } else if (nj != 1 && nj != n) {
        error(_("Inconsistent input lengths -- first found %d, but %d element has length %d. Only singletons will be recycled."), n, j+1, nj);
      }
    } else { // initialize
      n = LENGTH(xj);
    }
  }

  SEXP out = PROTECT(allocVector(outtype, n));
  if (n == 0) {
    UNPROTECT(1);
    return(out);
  }
  if (LOGICAL(narmArg)[0]) {
    switch (outtype) {
    case INTSXP: {
      int *outp = INTEGER(out), *xjp;
      for (int i=0; i<n; i++) {
        int j = 0;
        // speed through columns until find non-missing
        while (j < J) {
          xj = VECTOR_ELT(x, j++);
          xjp = INTEGER(xj); // INTEGER is the same as LOGICAL
          nj = LENGTH(xj);
          xi = nj == 1 ? 0 : i; // recycling for singletons
          if (xjp[xi] != NA_INTEGER) {
            outp[i] = xjp[xi]; // initialize
            break;
          }
        }
        if (j == J && xjp[xi] == NA_INTEGER) { // default
          outp[i] = NA_INTEGER;
          break;
        }
        for ( ; j<J; j++) {
          xj = VECTOR_ELT(x, j);
          xjp = INTEGER(xj);
          nj = LENGTH(xj);
          xi = nj == 1 ? 0 : i;
          if (xjp[xi] == NA_INTEGER)
            continue;
          if ((xjp[xi] > 0 && INT_MAX - xjp[xi] < outp[i]) ||
              (xjp[xi] < 0 && INT_MIN - xjp[xi] > outp[i])) { // overflow
            outp[i] = NA_INTEGER;
            warning(_("Inputs have exceeded .Machine$integer.max=%d in absolute value; returning NA. Please cast to numeric first to avoid this."), INT_MAX);
            break;
          }
          outp[i] += xjp[xi];
        }
      }
    } break;
    case REALSXP: { // REALSXP; special handling depending on whether each input is int/numeric
      double *outp = REAL(out), *xjp; // since outtype is REALSXP, there's at least one REAL column
      for (int i=0; i<n; i++) {
        int j=0, initialized=0;
        // speed through columns until find non-missing
        while (j < J && !initialized) {
          xj = VECTOR_ELT(x, j++);
          nj = LENGTH(xj);
          xi = nj == 1 ? 0 : i;
          switch(TYPEOF(xj)) {
          case LGLSXP: case INTSXP: {
            int *xjp = INTEGER(xj);
            if (xjp[xi] != NA_INTEGER) {
              initialized = 1;
              outp[i] = (double)xjp[xi]; // initialize
              break;
            }
          } break;
          case REALSXP: {
            xjp = REAL(xj);
            if (!ISNAN(xjp[xi])) {
              initialized = 1;
              outp[i] = xjp[xi]; // initialize
              break;
            }
          } break;
          default:
            error(_("Internal error: should have caught invalid input by now")); // # nocov
          }
        }
        if (j == J && !initialized) {
          outp[i] = NA_REAL;
          break;
        }
        for ( ; j<J; j++) {
          xj = VECTOR_ELT(x, j);
          nj = LENGTH(xj);
          xi = nj == 1 ? 0 : i;
          switch (TYPEOF(xj)) {
          case LGLSXP: case INTSXP: {
            int *xjp = INTEGER(xj);
            if (xjp[xi] == NA_INTEGER)
              continue;
            outp[i] += xjp[xi];
          } break;
          case REALSXP: {
            xjp = REAL(xj);
            if (ISNAN(xjp[xi]))
              continue;
            outp[i] += xjp[xi];
          } break;
          default:
            error(_("Internal error: should have caught invalid input by now")); // # nocov
          }
        }
      }
    } break;
    case CPLXSXP: {
      Rcomplex *outp = COMPLEX(out), *xjp;
      for (int i=0; i<n; i++) {
        int j=0, initialized=0;
        // speed through columns until find non-missing
        while (j < J && !initialized) {
          xj = VECTOR_ELT(x, j++);
          nj = LENGTH(xj);
          xi = nj == 1 ? 0 : i;
          switch(TYPEOF(xj)) {
          case LGLSXP: case INTSXP: {
            int *xjp = INTEGER(xj);
            if (xjp[xi] != NA_INTEGER) {
              initialized = 1;
              outp[i].r = (double)xjp[xi]; // initialize
              outp[i].i = 0;
              break;
            }
          } break;
          case REALSXP: {
            double *xjp = REAL(xj);
            if (!ISNAN(xjp[xi])) {
              initialized = 1;
              outp[i].r = xjp[xi]; // initialize
              outp[i].i = 0;
              break;
            }
          } break;
          case CPLXSXP: {
            xjp = COMPLEX(xj);
            if (!ISNAN_COMPLEX(xjp[xi])) {
              initialized = 1;
              outp[i].r = xjp[xi].r;
              outp[i].i = xjp[xi].i;
            }
          } break;
          default:
            error(_("Internal error: should have caught invalid input by now")); // # nocov
          }
        }
        if (j == J && !initialized) { // default
          outp[i] = NA_CPLX;
          break;
        }
        for ( ; j<J; j++) {
          xj = VECTOR_ELT(x, j);
          nj = LENGTH(xj);
          xi = nj == 1 ? 0 : i;
          switch (TYPEOF(xj)) {
          case LGLSXP: case INTSXP: {
            int *xjp = INTEGER(xj);
            if (xjp[xi] == NA_INTEGER)
              continue;
            outp[i].r += xjp[xi];
          } break;
          case REALSXP: {
            double *xjp = REAL(xj);
            if (ISNAN(xjp[xi]))
              continue;
            outp[i].r += xjp[xi];
          } break;
          case CPLXSXP: {
            xjp = COMPLEX(xj);
            if (ISNAN_COMPLEX(xjp[xi]))
              continue;
            outp[i].r += xjp[xi].r;
            outp[i].i += xjp[xi].i;
          } break;
          default:
            error(_("Internal error: should have caught invalid input by now")); // # nocov
          }
        }
      }
    } break;
    default:
      error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
    }
  } else { // na.rm=FALSE
    switch (outtype) {
    case INTSXP: {
      int *outp = INTEGER(out), *xjp;
      int sum;
      for (int i=0; i<n; i++) {
        sum = 0;
        for (int j=0; j<J; j++) {
          xj = VECTOR_ELT(x, j);
          nj = LENGTH(xj);
          xjp = INTEGER(xj);
          xi = nj == 1 ? 0 : i;
          if (xjp[xi] == NA_INTEGER) {
            sum = NA_INTEGER;
            break;
          }
          if ((xjp[xi] > 0 && INT_MAX - xjp[xi] < sum) ||
              (xjp[xi] < 0 && INT_MIN - xjp[xi] > sum)) {
            warning(_("Inputs have exceeded .Machine$integer.max=%d in absolute value; returning NA. Please cast to numeric first to avoid this."), INT_MAX);
            sum = NA_INTEGER;
            break;
          }
          sum += xjp[xi];
        }
        outp[i] = sum;
      }
    } break;
    case REALSXP: {
      double *outp = REAL(out), *xjp;
      double sum;
      for (int i=0; i<n; i++) {
        sum = 0;
        int j = 0, is_na = 0;
        while (j < J && !is_na) {
          xj = VECTOR_ELT(x, j++);
          nj = LENGTH(xj);
          xi = nj == 1 ? 0 : i;
          switch (TYPEOF(xj)) {
          case LGLSXP: case INTSXP: {
            int *xjp = INTEGER(xj);
            if (xjp[xi] == NA_INTEGER) {
              is_na = 1;
              break;
            }
            sum += xjp[xi];
          } break;
          case REALSXP: {
            xjp = REAL(xj);
            if (ISNAN(xjp[xi])) {
              is_na = 1;
              break;
            }
            sum += xjp[xi];
          } break;
          default:
            error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
          }
        }
        outp[i] = is_na ? NA_REAL : sum;
      }
    } break;
    case CPLXSXP: {
      Rcomplex *outp = COMPLEX(out), *xjp;
      double rsum, isum;
      for (int i=0; i<n; i++) {
        rsum = isum = 0;
        int j = 0, is_na = 0;
        while (j < J && !is_na) {
          xj = VECTOR_ELT(x, j++);
          nj = LENGTH(xj);
          xi = nj == 1 ? 0 : i;
          switch (TYPEOF(xj)) {
          case LGLSXP: case INTSXP: {
            int *xjp = INTEGER(xj);
            if (xjp[xi] == NA_INTEGER) {
              is_na = 1;
              break;
            }
            rsum += xjp[xi];
          } break;
          case REALSXP: {
            double *xjp = REAL(xj);
            if (ISNAN(xjp[xi])) {
              is_na = 1;
              break;
            }
            rsum += xjp[xi];
          } break;
          case CPLXSXP: {
            xjp = COMPLEX(xj);
            if (ISNAN_COMPLEX(xjp[xi])) {
              is_na = 1;
              break;
            }
            rsum += xjp[xi].r;
            isum += xjp[xi].i;
          } break;
          default:
            error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
          }
        }
        if (is_na) {
          outp[i] = NA_CPLX;
        } else {
          outp[i].r = rsum;
          outp[i].i = isum;
        }
      }
    } break;
    default:
      error(_("Internal error: should have caught non-INTSXP/REALSXP input by now")); // # nocov
    }
  }

  UNPROTECT(1);
  return out;
}
