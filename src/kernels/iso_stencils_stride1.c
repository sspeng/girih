#include "data_structures.h"

#define U(i)         (u_s[(i)])
#define V(i,j,k)     (v_s[((k)*(clu_ctx.nny)+(j))*(clu_ctx.nnx)+(i)])
#define ROC2(i)      (roc2_s[(i)])
#define COEF(m,i,j,k) (coef[((k)*(clu_ctx.nny)+(j))*(clu_ctx.nnx)+(i)+(((unsigned long) (clu_ctx.ln_domain))*(m))])


// ISO stencils spatial blocking
void iso_ref_split( const int shape[3], const int xb, const int yb, const int zb, const int xe, const int ye, const int ze,
    const FLOAT_PRECISION * restrict coef, FLOAT_PRECISION * restrict u,
    const FLOAT_PRECISION * restrict v, const FLOAT_PRECISION * restrict roc2, stencil_CTX stencil_ctx) {
  int j,k, jb, je;

  CLU_CTX clu_ctx;
  clu_ctx.nnx = shape[0];
  clu_ctx.nny = shape[1];
  clu_ctx.nnz = shape[2];
  clu_ctx.xb = xb;
  clu_ctx.xe = xe;
  clu_ctx.ln_domain = shape[0]*shape[1]*shape[2];

  for(jb=yb; jb<ye; jb+=stencil_ctx.bs_y) // blocking in Y
  {
    je = (jb+stencil_ctx.bs_y)<(ye)?(jb+stencil_ctx.bs_y):(ye);
#pragma omp parallel num_threads(stencil_ctx.thread_group_size)
    {
#pragma omp for private(k,j) schedule(static,1)
      for(k=zb; k<ze; k++) {
        for(j=jb; j<je; j++) {
          clu_ctx.j = j;
          clu_ctx.k = k;
          stencil_ctx.clu_func(clu_ctx, coef, u, v, roc2);
        }
      }
    }
  }
}


// 1WD kernel
void swd_iso_ref_split( const int shape[3], const int xb, const int yb_r, const int zb, const int xe, const int ye_r, const int ze,
    const FLOAT_PRECISION * restrict coef, FLOAT_PRECISION * restrict u,
    FLOAT_PRECISION * restrict v, const FLOAT_PRECISION * restrict roc2, int t_dim, int b_inc, int e_inc, int NHALO, stencil_CTX stencil_ctx, int mtid){

  int i, j, k, t, yb, ye, zi, kt, ib, ie, ib_r, ie_r, bs_x;
  int nny =shape[1];
  int nnx =shape[0];
  int nwf = stencil_ctx.num_wf;

  int time_blk = t_dim*2+1; //temporal block size

  CLU_CTX clu_ctx;
  clu_ctx.nnx = shape[0];
  clu_ctx.nny = shape[1];
  clu_ctx.nnz = shape[2];
  clu_ctx.ln_domain = shape[0]*shape[1]*shape[2];

  if (zb+nwf >= ze) nwf = ze-zb;
  bs_x = stencil_ctx.bs_x;
  for(ib_r=xb; ib_r<xe; ib_r+=bs_x) { // blocking in X
    ie_r = (ib_r+bs_x)<(xe)?(ib_r+bs_x):(xe);
    //printf("bs_x:%d  xb:%d  xe:%d  ib:%d  ie:%d\n", bs_x, xb, xe, ib, ie);
    for(zi=zb; zi<ze; zi+=nwf) { // wavefront loop
      if(zi+nwf >= ze) nwf = ze-zi;
      yb = yb_r;
      ye = ye_r;
      ib = ib_r;
      ie = ie_r;

      kt = zi;
      for(t=0; t< time_blk; t++){
        if((t)%2 == 1){

          for(k=kt; k<nwf+kt; k++){
            clu_ctx.k = k;
            for(j=yb; j<ye; j++) {
              clu_ctx.j = j;
              clu_ctx.xb = ib;
              clu_ctx.xe = ie;
              stencil_ctx.clu_func(clu_ctx, coef, u, v, roc2);
            }
          }

        }else{

          for(k=kt; k<nwf+kt; k++){
            clu_ctx.k = k;
            for(j=yb; j<ye; j++) {
              clu_ctx.j = j;
              clu_ctx.xb = ib;
              clu_ctx.xe = ie;
              stencil_ctx.clu_func(clu_ctx, coef, v, u, roc2);
            }
          }

        }

        // Update block size in Y
        if(t< t_dim){ // inverted trapezoid (or lower half of the diamond)
          yb -= b_inc;
          ye += e_inc;
        }else{ // trapezoid  (or upper half of the diamond)
          yb += b_inc;
          ye -= e_inc;
        }

        // Update block size in X
        if (ib != xb) ib-=NHALO;
        if (ie != xe) ie-=NHALO;

        kt -= NHALO;

      } // time loop
    } // wavefront loop
  } // blocking in x
}


void mwd_iso_ref_split( const int shape[3], const int xb, const int yb_r, const int zb, const int xe, const int ye_r, const int ze,
    const FLOAT_PRECISION * restrict coef, FLOAT_PRECISION * restrict u,
    FLOAT_PRECISION * restrict v, const FLOAT_PRECISION * restrict roc2, int t_dim, int b_inc, int e_inc, int NHALO, stencil_CTX stencil_ctx, int mtid) {

  double t_start;
  int i, j, k, t, zi, kt, yb, ye, tid, not_done, gtid, thb, the, q, r, ib, ie, ib_r, ie_r;
  int nny =shape[1];
  int nnx =shape[0];
  int nwf = stencil_ctx.num_wf;
  int time_blk = t_dim*2+1; //temporal block size
  int tgs = stencil_ctx.thread_group_size;
  int th_nwf = nwf/tgs;
  int bs_x = stencil_ctx.bs_x;

#pragma omp parallel private(ib_r, ie_r, ib, ie, zi, kt, yb, ye, t, q, r, thb, the, tid, gtid, i, j, k, not_done) shared(bs_x, tgs, nwf, th_nwf, zb, ze, yb_r, ye_r, mtid, xb, xe, time_blk, t_dim, b_inc, e_inc, t_start) num_threads(stencil_ctx.thread_group_size)
  {


    tid = 0;
    gtid = 0;
#if defined(_OPENMP)
    tid = omp_get_thread_num();
    gtid = tid + mtid * tgs;
#endif

    CLU_CTX clu_ctx;
    clu_ctx.nnx = shape[0];
    clu_ctx.nny = shape[1];
    clu_ctx.nnz = shape[2];
    clu_ctx.ln_domain = shape[0]*shape[1]*shape[2];


    ib_r = xb;
    while(ib_r < xe){ // blocking in X
      ie_r = (ib_r+bs_x)<(xe)?(ib_r+bs_x):(xe);
      ib = ib_r;
      ie = ie_r;

      not_done = 1;
      zi = zb;
      kt = zb;
      t = 0;
      yb = yb_r;
      ye = ye_r;
      thb = th_nwf*tid;
      the = th_nwf*(tid+1);
      while(not_done) { // wavrfront loop

  //if(t==0)
  //if(tid==1)
//  printf("[%d, %d]ib_r:%d ie_r:%d ib:%d ie:%d bs_x:%d t:%d yb_r:%d, ye_r:%d yb:%d ye:%d thb:%d the:%d nwf:%d zi:%d kt:%d \n",
//                      gtid, tid, ib_r, ie_r, ib, ie, bs_x, t, yb_r, ye_r, yb, ye, thb, the, nwf, zi, kt);

          if((t)%2 == 1){
            for(k=kt+thb; k<kt+the; k++){
              clu_ctx.k = k;
              for(j=yb; j<ye; j++) {
                clu_ctx.j = j;
                clu_ctx.xb = ib;
                clu_ctx.xe = ie;
                stencil_ctx.clu_func(clu_ctx, coef, u, v, roc2);
              }
            }
          }else{
            for(k=kt+thb; k<kt+the; k++){
              clu_ctx.k = k;
              for(j=yb; j<ye; j++) {
                clu_ctx.j = j;
                clu_ctx.xb = ib;
                clu_ctx.xe = ie;
                stencil_ctx.clu_func(clu_ctx, coef, v, u, roc2);
              }
            }
          }
  //      }

          if(t+1 < time_blk){

            // Update block size in Y
            if(t< t_dim){ // inverted trapezoid (or lower half of the diamond)
              yb -= b_inc;
              ye += e_inc;
            }else{ // trapezoid  (or upper half of the diamond)
              yb += b_inc;
              ye -= e_inc;
            }
            kt -= NHALO;
            t++;

            // Update block size in X
            if (ib != xb) ib-=NHALO;
            if (ie != xe) ie-=NHALO;

          } else {
  //printf("\n");
            t = 0;
            yb = yb_r;
            ye = ye_r;

            ib = ib_r;
            ie = ie_r;

            zi+=nwf;
            kt = zi;
            if(zi >= ze) not_done = 0;
          }

        // reassign the wavefronts to cores if fraction of MW remains
        if( ((ze-zi) < nwf) & (t == 0)){
          q = (int)((ze-zi)/tgs);
          r = (ze-zi)%tgs;
          if(tid < r) {
            thb = tid * (q+1);
            the = thb + (q+1);
          }else {
            thb = r * (q+1) + (tid - r) * q;
            the =thb + q;
          }
  //if(not_done == 1) printf("[%d]  q:%d r:%d thb:%d the:%d rem:%d\n", tid, q, r, thb, the, ze-zi);
        }

        t_start = MPI_Wtime();
  #pragma omp barrier
        stencil_ctx.t_wait[gtid] += MPI_Wtime() - t_start;

      } // z loop (wavefront)

      // move to next block in X
      ib_r += bs_x;
    } // Blocking in X loop

  } // parallel region

//printf("\n");
}


// 1-stide of ISO stencil 8th-order-in-space-2nd-order-in-time with constant coefficient
void iso_ref_clu_8space_2time CLU_SIG{

  int i;
  FLOAT_PRECISION two=2.0;
  FLOAT_PRECISION lap;
  FLOAT_PRECISION * restrict u_s    =    &u[(clu_ctx.j + clu_ctx.k*clu_ctx.nny)*clu_ctx.nnx];
  const FLOAT_PRECISION * restrict v_s    =    &v[(clu_ctx.j + clu_ctx.k*clu_ctx.nny)*clu_ctx.nnx];
  const FLOAT_PRECISION * restrict roc2_s = &roc2[(clu_ctx.j + clu_ctx.k*clu_ctx.nny)*clu_ctx.nnx];

#pragma simd
  for(i=clu_ctx.xb; i<clu_ctx.xe; i++) {

    lap=coef[0]*V(i,0,0)                    // MOVE THE MUL OUT
    +coef[1]*(V(i+1,0  ,0  )+V(i-1,0  ,0  ))
    +coef[1]*(V(i  , +1,0  )+V(i  , -1,0  ))
    +coef[1]*(V(i  ,0  , +1)+V(i  ,0  , -1))
    +coef[2]*(V(i+2,0  ,0  )+V(i-2,0  ,0  ))
    +coef[2]*(V(i  , +2,0  )+V(i  , -2,0  ))
    +coef[2]*(V(i  ,0  , +2)+V(i  ,0  , -2))
    +coef[3]*(V(i+3,0  ,0  )+V(i-3,0  ,0  ))
    +coef[3]*(V(i  , +3,0  )+V(i  , -3,0  ))
    +coef[3]*(V(i  ,0  , +3)+V(i  ,0  , -3))
    +coef[4]*(V(i+4,0  ,0  )+V(i-4,0  ,0  ))
    +coef[4]*(V(i  , +4,0  )+V(i  , -4,0  ))
    +coef[4]*(V(i  ,0  , +4)+V(i  ,0  , -4));

    U(i) = two*V(i,0,0) - U(i) + ROC2(i)*lap;
  }
}


// 1-stride of ISO stencil 2nd-order-in-space-1st-order-in-time with constant coefficient
void iso_ref_clu_2space_1time CLU_SIG{

  int i;
  FLOAT_PRECISION * restrict u_s       = &u[(clu_ctx.j + clu_ctx.k*clu_ctx.nny)*clu_ctx.nnx];
  const FLOAT_PRECISION * restrict v_s = &v[(clu_ctx.j + clu_ctx.k*clu_ctx.nny)*clu_ctx.nnx];

#pragma simd
  for(i=clu_ctx.xb; i<clu_ctx.xe; i++) {
    U(i) = coef[0]*V(i,0,0)
          +coef[1]*(V(i+1,0  ,0  )+V(i-1,0  ,0  ))
          +coef[1]*(V(i  , +1,0  )+V(i  , -1,0  ))
          +coef[1]*(V(i  ,0  , +1)+V(i  ,0  , -1));
  }
}


// 1-stride of ISO stencil 2nd-order-in-space-1st-order-in-time with variable coefficient
void iso_ref_clu_2space_1time_var CLU_SIG{

  int i;
  int nxny = clu_ctx.nnx*clu_ctx.nny;
  int stride_start = clu_ctx.j*clu_ctx.nnx + clu_ctx.k*nxny;
  FLOAT_PRECISION * restrict u_s       = &u[stride_start];
  const FLOAT_PRECISION * restrict v_s = &v[stride_start];
  const FLOAT_PRECISION * restrict coef0_s = &coef[stride_start];
  const FLOAT_PRECISION * restrict coef1_s = &coef[stride_start + clu_ctx.ln_domain];
#pragma simd
  for(i=clu_ctx.xb; i<clu_ctx.xe; i++) {
    U(i) = coef0_s[i]*V(i,0,0)
          +coef1_s[i]*(V(i+1,0  ,0  )+V(i-1,0  ,0  ))
          +coef1_s[i]*(V(i  , +1,0  )+V(i  , -1,0  ))
          +coef1_s[i]*(V(i  ,0  , +1)+V(i  ,0  , -1));
  }
}

// 1-stride of ISO stencil 2nd-order-in-space-1st-order-in-time with variable coefficient
void iso_ref_clu_2space_1time_var_axsym CLU_SIG{

  int i;
  int nxny = clu_ctx.nnx*clu_ctx.nny;
  int stride_start = clu_ctx.j*clu_ctx.nnx + clu_ctx.k*nxny;
  FLOAT_PRECISION * restrict u_s       = &u[stride_start];
  const FLOAT_PRECISION * restrict v_s = &v[stride_start];
  const FLOAT_PRECISION * restrict coef0_s = &coef[stride_start];
  const FLOAT_PRECISION * restrict coef1_s = &coef[stride_start + clu_ctx.ln_domain];
  const FLOAT_PRECISION * restrict coef2_s = &coef[stride_start + clu_ctx.ln_domain*2];
  const FLOAT_PRECISION * restrict coef3_s = &coef[stride_start + clu_ctx.ln_domain*3];
#pragma simd
  for(i=clu_ctx.xb; i<clu_ctx.xe; i++) {
    U(i) = coef0_s[i]*V(i,0,0)
          +coef1_s[i]*(V(i+1,0  ,0  )+V(i-1,0  ,0  ))
          +coef2_s[i]*(V(i  , +1,0  )+V(i  , -1,0  ))
          +coef3_s[i]*(V(i  ,0  , +1)+V(i  ,0  , -1));
  }
}


void iso_ref_clu_8space_1time_var_axsym CLU_SIG{

  int i;
  int nxny = clu_ctx.nnx*clu_ctx.nny;
  int stride_start = clu_ctx.j*clu_ctx.nnx + clu_ctx.k*nxny;
  FLOAT_PRECISION * restrict u_s       = &u[stride_start];
  const FLOAT_PRECISION * restrict v_s = &v[stride_start];
  const FLOAT_PRECISION * restrict coef0_s = &coef[stride_start];
  const FLOAT_PRECISION * restrict coef1_s = &coef[stride_start + clu_ctx.ln_domain];
  const FLOAT_PRECISION * restrict coef2_s = &coef[stride_start + clu_ctx.ln_domain*2];
  const FLOAT_PRECISION * restrict coef3_s = &coef[stride_start + clu_ctx.ln_domain*3];
  const FLOAT_PRECISION * restrict coef4_s = &coef[stride_start + clu_ctx.ln_domain*4];
  const FLOAT_PRECISION * restrict coef5_s = &coef[stride_start + clu_ctx.ln_domain*5];
  const FLOAT_PRECISION * restrict coef6_s = &coef[stride_start + clu_ctx.ln_domain*6];
  const FLOAT_PRECISION * restrict coef7_s = &coef[stride_start + clu_ctx.ln_domain*7];
  const FLOAT_PRECISION * restrict coef8_s = &coef[stride_start + clu_ctx.ln_domain*8];
  const FLOAT_PRECISION * restrict coef9_s = &coef[stride_start + clu_ctx.ln_domain*9];
  const FLOAT_PRECISION * restrict coef10_s= &coef[stride_start + clu_ctx.ln_domain*10];
  const FLOAT_PRECISION * restrict coef11_s= &coef[stride_start + clu_ctx.ln_domain*11];
  const FLOAT_PRECISION * restrict coef12_s= &coef[stride_start + clu_ctx.ln_domain*12];

#pragma simd
  for(i=clu_ctx.xb; i<clu_ctx.xe; i++) {
    U(i) = coef0_s[i]*V(i,0,0)
          +coef1_s[i]*(V(i+1,0  ,0  )+V(i-1,0  ,0  ))
          +coef2_s[i]*(V(i  , +1,0  )+V(i  , -1,0  ))
          +coef3_s[i]*(V(i  ,0  , +1)+V(i  ,0  , -1))
          +coef4_s[i]*(V(i+2,0  ,0  )+V(i-2,0  ,0  ))
          +coef5_s[i]*(V(i  , +2,0  )+V(i  , -2,0  ))
          +coef6_s[i]*(V(i  ,0  , +2)+V(i  ,0  , -2))
          +coef7_s[i]*(V(i+3,0  ,0  )+V(i-3,0  ,0  ))
          +coef8_s[i]*(V(i  , +3,0  )+V(i  , -3,0  ))
          +coef9_s[i]*(V(i  ,0  , +3)+V(i  ,0  , -3))
         +coef10_s[i]*(V(i+4,0  ,0  )+V(i-4,0  ,0  ))
         +coef11_s[i]*(V(i  , +4,0  )+V(i  , -4,0  ))
         +coef12_s[i]*(V(i  ,0  , +4)+V(i  ,0  , -4));


  }
}

// 1-stride of ISO stencil 2nd-order-in-space-1st-order-in-time with variable coefficient
void iso_ref_clu_2space_1time_var_nosym CLU_SIG{

  int i;
  int nxny = clu_ctx.nnx*clu_ctx.nny;
  int stride_start = clu_ctx.j*clu_ctx.nnx + clu_ctx.k*nxny;
  FLOAT_PRECISION * restrict u_s       = &u[stride_start];
  const FLOAT_PRECISION * restrict v_s = &v[stride_start];
  const FLOAT_PRECISION * restrict coef0_s = &coef[stride_start];
  const FLOAT_PRECISION * restrict coef1_s = &coef[stride_start + clu_ctx.ln_domain];
  const FLOAT_PRECISION * restrict coef2_s = &coef[stride_start + clu_ctx.ln_domain*2];
  const FLOAT_PRECISION * restrict coef3_s = &coef[stride_start + clu_ctx.ln_domain*3];
  const FLOAT_PRECISION * restrict coef4_s = &coef[stride_start + clu_ctx.ln_domain*4];
  const FLOAT_PRECISION * restrict coef5_s = &coef[stride_start + clu_ctx.ln_domain*5];
  const FLOAT_PRECISION * restrict coef6_s = &coef[stride_start + clu_ctx.ln_domain*6];
#pragma simd
  for(i=clu_ctx.xb; i<clu_ctx.xe; i++) {
    U(i) = coef0_s[i]*V(i  ,0 ,0 )
          +coef1_s[i]*V(i-1,0 ,0 )
          +coef2_s[i]*V(i+1,0 ,0 )
          +coef3_s[i]*V(i  ,-1,0 )
          +coef4_s[i]*V(i  ,+1,0 )
          +coef5_s[i]*V(i  ,0 ,-1)
          +coef6_s[i]*V(i  ,0 ,+1);
  }
}

clu_func_t clu_func_list[] = {
    iso_ref_clu_8space_2time,
    iso_ref_clu_2space_1time,
    iso_ref_clu_2space_1time_var,
    iso_ref_clu_2space_1time_var_axsym,
    iso_ref_clu_8space_1time_var_axsym,
    iso_ref_clu_2space_1time_var_nosym,
};

