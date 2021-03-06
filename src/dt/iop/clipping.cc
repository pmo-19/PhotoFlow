/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.
    copyright (c) 2012 henrik andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

/** region of interest */
typedef struct dt_iop_roi_t
{
  int x, y, width, height;
  float scale;
} dt_iop_roi_t;


typedef struct dt_iop_clipping_params_t
{
  float angle, cx, cy, cw, ch, k_h, k_v;
  float kxa, kya, kxb, kyb, kxc, kyc, kxd, kyd;
  int k_type, k_sym;
  int k_apply, crop_auto;
  int ratio_n, ratio_d;
} dt_iop_clipping_params_t;


typedef struct dt_iop_clipping_data_t
{
  float angle;              // rotation angle
  float aspect;             // forced aspect ratio
  float m[4];               // rot matrix
  float ki_h, k_h;          // keystone correction, ki and corrected k
  float ki_v, k_v;          // keystone correction, ki and corrected k
  float tx, ty;             // rotation center
  float cx, cy, cw, ch;     // crop window
  float cix, ciy, ciw, cih; // crop window on roi_out 1.0 scale
  uint32_t all_off;         // 1: v and h off, else one of them is used
  uint32_t flags;           // flipping flags
  uint32_t flip;            // flipped output buffer so more area would fit.

  float k_space[4]; // space for the "destination" rectangle of the keystone quadrilatere
  float kxa, kya, kxb, kyb, kxc, kyc, kxd,
      kyd; // point of the "source" quadrilatere (modified if keystone is not "full")
  float a, b, d, e, g, h; // value of the transformation matrix (c=f=0 && i=1)
  int k_apply;
  int crop_auto;
  float enlarge_x, enlarge_y;
  float offset_x, offset_y;
} dt_iop_clipping_data_t;


static void mul_mat_vec_2(const float *m, const float *p, float *o)
{
  o[0] = p[0] * m[0] + p[1] * m[1];
  o[1] = p[0] * m[2] + p[1] * m[3];
}

// helper to count corners in for loops:
static void get_corner(const float *aabb, const int i, float *p)
{
  for(int k = 0; k < 2; k++) p[k] = aabb[2 * ((i >> k) & 1) + k];
}

static void adjust_aabb(const float *p, float *aabb)
{
  aabb[0] = fminf(aabb[0], p[0]);
  aabb[1] = fminf(aabb[1], p[1]);
  aabb[2] = fmaxf(aabb[2], p[0]);
  aabb[3] = fmaxf(aabb[3], p[1]);
}

static void keystone_get_matrix(float *k_space, float kxa, float kxb, float kxc, float kxd, float kya,
                                float kyb, float kyc, float kyd, float *a, float *b, float *d, float *e,
                                float *g, float *h)
{
  *a = -((kxb * (kyd * kyd - kyc * kyd) - kxc * kyd * kyd + kyb * (kxc * kyd - kxd * kyd) + kxd * kyc * kyd)
         * k_space[2])
       / (kxb * (kxc * kyd * kyd - kxd * kyc * kyd) + kyb * (kxd * kxd * kyc - kxc * kxd * kyd));
  *b = ((kxb * (kxd * kyd - kxd * kyc) - kxc * kxd * kyd + kxd * kxd * kyc + (kxc * kxd - kxd * kxd) * kyb)
        * k_space[2])
       / (kxb * (kxc * kyd * kyd - kxd * kyc * kyd) + kyb * (kxd * kxd * kyc - kxc * kxd * kyd));
  *d = (kyb * (kxb * (kyd * k_space[3] - kyc * k_space[3]) - kxc * kyd * k_space[3] + kxd * kyc * k_space[3])
        + kyb * kyb * (kxc * k_space[3] - kxd * k_space[3]))
       / (kxb * kyb * (-kxc * kyd - kxd * kyc) + kxb * kxb * kyc * kyd + kxc * kxd * kyb * kyb);
  *e = -(kxb * (kxd * kyc * k_space[3] - kxc * kyd * k_space[3])
         + kxb * kxb * (kyd * k_space[3] - kyc * k_space[3])
         + kxb * kyb * (kxc * k_space[3] - kxd * k_space[3]))
       / (kxb * kyb * (-kxc * kyd - kxd * kyc) + kxb * kxb * kyc * kyd + kxc * kxd * kyb * kyb);
  *g = -(kyb * (kxb * (2.0f * kxc * kyd * kyd - 2.0f * kxc * kyc * kyd) - kxc * kxc * kyd * kyd
                + 2.0f * kxc * kxd * kyc * kyd - kxd * kxd * kyc * kyc)
         + kxb * kxb * (kyc * kyc * kyd - kyc * kyd * kyd)
         + kyb * kyb * (-2.0f * kxc * kxd * kyd + kxc * kxc * kyd + kxd * kxd * kyc))
       / (kxb * kxb * (kxd * kyc * kyc * kyd - kxc * kyc * kyd * kyd)
          + kxb * kyb * (kxc * kxc * kyd * kyd - kxd * kxd * kyc * kyc)
          + kyb * kyb * (kxc * kxd * kxd * kyc - kxc * kxc * kxd * kyd));
  *h = (kxb * (-kxc * kxc * kyd * kyd + 2.0f * kxc * kxd * kyc * kyd - kxd * kxd * kyc * kyc)
        + kxb * kxb * (kxc * kyd * kyd - 2.0f * kxd * kyc * kyd + kxd * kyc * kyc)
        + kxb * (2.0f * kxd * kxd - 2.0f * kxc * kxd) * kyb * kyc
        + (kxc * kxc * kxd - kxc * kxd * kxd) * kyb * kyb)
       / (kxb * kxb * (kxd * kyc * kyc * kyd - kxc * kyc * kyd * kyd)
          + kxb * kyb * (kxc * kxc * kyd * kyd - kxd * kxd * kyc * kyc)
          + kyb * kyb * (kxc * kxd * kxd * kyc - kxc * kxc * kxd * kyd));
}

static void keystone_backtransform(float *i, float *k_space, float a, float b, float d, float e, float g,
                                   float h, float kxa, float kya)
{
  float xx = i[0] - k_space[0];
  float yy = i[1] - k_space[1];

  float div = ((d * xx - a * yy) * h + (b * yy - e * xx) * g + a * e - b * d);

  i[0] = (e * xx - b * yy) / div + kxa;
  i[1] = -(d * xx - a * yy) / div + kya;
}

static int keystone_transform(float *i, float *k_space, float a, float b, float d, float e, float g, float h,
                              float kxa, float kya)
{
  float xx = i[0] - kxa;
  float yy = i[1] - kya;

  float div = g * xx + h * yy + 1;
  i[0] = (a * xx + b * yy) / div + k_space[0];
  i[1] = (d * xx + e * yy) / div + k_space[1];
  return 1;
}

// 1st pass: how large would the output be, given this input roi?
// this is always called with the full buffer before processing.
void modify_roi_out(//struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece,
    struct dt_iop_clipping_data_t *d, dt_iop_roi_t *roi_out,
    const dt_iop_roi_t *roi_in_orig)
{
  printf("modify_roi_out(): roi_in=%d,%d\n",roi_in_orig->width,roi_in_orig->height);
  dt_iop_roi_t roi_in_d = *roi_in_orig;
  dt_iop_roi_t *roi_in = &roi_in_d;

  //dt_iop_clipping_data_t *d = (dt_iop_clipping_data_t *)piece->data;

  *roi_out = *roi_in;
  // set roi_out values with rotation and keystone
  // initial corners pos
  float corn_x[4] = { 0.0f, roi_in->width, roi_in->width, 0.0f };
  float corn_y[4] = { 0.0f, 0.0f, roi_in->height, roi_in->height };
  // destination corner points
  float corn_out_x[4] = { 0.0f };
  float corn_out_y[4] = { 0.0f };

  // we don't test image flip as autocrop is not completely ok...
  d->flip = 0;

  // we apply rotation and keystone to all those points
  float p[2], o[2];
  for(int c = 0; c < 4; c++)
  {
    printf("corn_x[%d]=%f  corn_y[%d]=%f\n",
        c,corn_x[c],c,corn_y[c]);
    // keystone
    o[0] = corn_x[c];
    o[1] = corn_y[c];
    printf("modify_roi_out(): #1 o(%d)=%f,%f\n",c,o[0],o[1]);
    o[0] /= (float)roi_in->width, o[1] /= (float)roi_in->height;
    printf("%f %f %f %f %f %f %f %f %f\n", d->a, d->b, d->d, d->e, d->g, d->h, d->kxa, d->kya);
    if(keystone_transform(o, d->k_space, d->a, d->b, d->d, d->e, d->g, d->h, d->kxa, d->kya) != 1)
    {
      // we set the point to maximum possible
      if(o[0] < 0.5f)
        o[0] = -1.0f;
      else
        o[0] = 2.0f;
      if(o[1] < 0.5f)
        o[1] = -1.0f;
      else
        o[1] = 2.0f;
    }
    o[0] *= roi_in->width, o[1] *= roi_in->height;
    printf("modify_roi_out(): #2 o(%d)=%f,%f\n",c,o[0],o[1]);

    // and we set the values
    corn_out_x[c] = o[0];
    corn_out_y[c] = o[1];
    printf("corn_out_x[%d]=%f  corn_out_y[%d]=%f\n",
        c,corn_out_x[c],c,corn_out_y[c]);
  }

  float new_x, new_y, new_sc_x, new_sc_y;
  new_x = fminf(fminf(fminf(corn_out_x[0], corn_out_x[1]), corn_out_x[2]), corn_out_x[3]);
  if(new_x + roi_in->width < 0) new_x = -roi_in->width;
  new_y = fminf(fminf(fminf(corn_out_y[0], corn_out_y[1]), corn_out_y[2]), corn_out_y[3]);
  if(new_y + roi_in->height < 0) new_y = -roi_in->height;
  printf("modify_roi_out(): new_x=%f  new_y=%f\n",new_x,new_y);

  new_sc_x = fmaxf(fmaxf(fmaxf(corn_out_x[0], corn_out_x[1]), corn_out_x[2]), corn_out_x[3]);
  if(new_sc_x > 2.0f * roi_in->width) new_sc_x = 2.0f * roi_in->width;
  new_sc_y = fmaxf(fmaxf(fmaxf(corn_out_y[0], corn_out_y[1]), corn_out_y[2]), corn_out_y[3]);
  if(new_sc_y > 2.0f * roi_in->height) new_sc_y = 2.0f * roi_in->height;
  printf("modify_roi_out(): #1 new_sc_x=%f  new_sc_y=%f\n",new_sc_x,new_sc_y);

  // be careful, we don't want too small area here !
  if(new_sc_x - new_x < roi_in->width / 8.0f)
  {
    float f = (new_sc_x + new_x) / 2.0f;
    if(f < roi_in->width / 16.0f) f = roi_in->width / 16.0f;
    if(f >= roi_in->width * 15.0f / 16.0f) f = roi_in->width * 15.0f / 16.0f - 1.0f;
    new_x = f - roi_in->width / 16.0f, new_sc_x = f + roi_in->width / 16.0f;
  }
  if(new_sc_y - new_y < roi_in->height / 8.0f)
  {
    float f = (new_sc_y + new_y) / 2.0f;
    if(f < roi_in->height / 16.0f) f = roi_in->height / 16.0f;
    if(f >= roi_in->height * 15.0f / 16.0f) f = roi_in->height * 15.0f / 16.0f - 1.0f;
    new_y = f - roi_in->height / 16.0f, new_sc_y = f + roi_in->height / 16.0f;
  }

  new_sc_y = new_sc_y - new_y;
  new_sc_x = new_sc_x - new_x;
  printf("modify_roi_out(): #2 new_sc_x=%f  new_sc_y=%f\n",new_sc_x,new_sc_y);

  // now we apply the clipping
  new_x += d->cx * new_sc_x;
  new_y += d->cy * new_sc_y;
  new_sc_x *= d->cw - d->cx;
  new_sc_y *= d->ch - d->cy;
  printf("modify_roi_out(): #3 new_x=%f  new_y=%f\n",new_x,new_y);
  printf("modify_roi_out(): #3 new_sc_x=%f  new_sc_y=%f\n",new_sc_x,new_sc_y);

  d->enlarge_x = fmaxf(-new_x, 0.0f);
  roi_out->x = fmaxf(new_x, 0.0f);
  d->enlarge_y = fmaxf(-new_y, 0.0f);
  roi_out->y = fmaxf(new_y, 0.0f);

  roi_out->width = new_sc_x;
  roi_out->height = new_sc_y;
  d->tx = roi_in->width * .5f;
  d->ty = roi_in->height * .5f;

  printf("modify_roi_out(): roi_out=%d,%d+%d+%d\n",roi_out->width,roi_out->height,roi_out->x,roi_out->y);
  printf("modify_roi_out(): d->enlarge_x=%f  d->enlarge_y=%f\n",(float)d->enlarge_x,(float)d->enlarge_y);

  // sanity check.
  if(roi_out->x < 0) roi_out->x = 0;
  if(roi_out->y < 0) roi_out->y = 0;
  if(roi_out->width < 1) roi_out->width = 1;
  if(roi_out->height < 1) roi_out->height = 1;

  // save rotation crop on output buffer in world scale:
  d->cix = roi_out->x;
  d->ciy = roi_out->y;
  d->ciw = roi_out->width;
  d->cih = roi_out->height;
}

// 2nd pass: which roi would this operation need as input to fill the given output region?
void modify_roi_in(struct dt_iop_clipping_data_t *d,
    const dt_iop_roi_t *roi_out, dt_iop_roi_t *roi_in,
    const float w, const float h)
{
  //dt_iop_clipping_data_t *d = (dt_iop_clipping_data_t *)piece->data;
  *roi_in = *roi_out;
  //return;
  // modify_roi_out took care of bounds checking for us. we hopefully do not get requests outside the clipping
  // area.
  // transform aabb back to roi_in

  // this aabb is set off by cx/cy
  const float so = 1.0f; //roi_out->scale;
  const float kw = w /*piece->buf_in.width * so*/, kh = h /*piece->buf_in.height * so*/;
  const float roi_out_x = roi_out->x - d->enlarge_x * so, roi_out_y = roi_out->y - d->enlarge_y * so;
  float p[2], o[2],
      aabb[4] = { roi_out_x + d->cix * so, roi_out_y + d->ciy * so, roi_out_x + d->cix * so + roi_out->width,
                  roi_out_y + d->ciy * so + roi_out->height };
  float aabb_in[4] = { INFINITY, INFINITY, -INFINITY, -INFINITY };
  for(int c = 0; c < 4; c++)
  {
    // get corner points of roi_out
    get_corner(aabb, c, p);

    /*
    // backtransform aabb using m
    if(d->flip)
    {
      p[1] -= d->tx * so;
      p[0] -= d->ty * so;
    }
    else
    {
      p[0] -= d->tx * so;
      p[1] -= d->ty * so;
    }
    p[0] *= 1.0 / so;
    p[1] *= 1.0 / so;
    backtransform(p, o, d->m, d->k_h, d->k_v);
    o[0] *= so;
    o[1] *= so;
    o[0] += d->tx * so;
    o[1] += d->ty * so;
    o[0] /= kw;
    o[1] /= kh;
    */
    o[0] = p[0];
    o[1] = p[1];
    o[0] /= kw;
    o[1] /= kh;
    if(d->k_apply == 1)
      keystone_backtransform(o, d->k_space, d->a, d->b, d->d, d->e, d->g, d->h, d->kxa, d->kya);
    o[0] *= kw;
    o[1] *= kh;
    // transform to roi_in space, get aabb.
    adjust_aabb(o, aabb_in);
  }

  // adjust roi_in to minimally needed region
  roi_in->x = aabb_in[0] - 1;
  roi_in->y = aabb_in[1] - 1;
  roi_in->width = aabb_in[2] - aabb_in[0] + 2;
  roi_in->height = aabb_in[3] - aabb_in[1] + 2;

  if(d->angle == 0.0f && d->all_off)
  {
    // just crop: make sure everything is precise.
    roi_in->x = aabb_in[0];
    roi_in->y = aabb_in[1];
    roi_in->width = roi_out->width;
    roi_in->height = roi_out->height;
  }

  // sanity check.
  const float scwidth = w/*piece->buf_in.width * so*/, scheight = h/*piece->buf_in.height * so*/;
  roi_in->x = CLAMP(roi_in->x, 0, (int)floorf(scwidth));
  roi_in->y = CLAMP(roi_in->y, 0, (int)floorf(scheight));
  roi_in->width = CLAMP(roi_in->width, 1, (int)ceilf(scwidth) - roi_in->x);
  roi_in->height = CLAMP(roi_in->height, 1, (int)ceilf(scheight) - roi_in->y);
}


void commit_params(//struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe,
                   //dt_dev_pixelpipe_iop_t *piece
    dt_iop_clipping_params_t *p, dt_iop_clipping_data_t *d
                   )
{
  //dt_iop_clipping_params_t *p = (dt_iop_clipping_params_t *)p1;
  //dt_iop_clipping_data_t *d = (dt_iop_clipping_data_t *)piece->data;

  // reset all values to be sure everything is initialized
  d->m[0] = d->m[3] = 1.0f;
  d->m[1] = d->m[2] = 0.0f;
  d->ki_h = d->ki_v = d->k_h = d->k_v = 0.0f;
  d->tx = d->ty = 0.0f;
  d->cix = d->ciy = 0.0f;
  d->cih = d->ciw = 1.0f;
  d->cx = d->cy = 0.0f;
  d->ch = d->cw = 1.0f;
  d->kxa = d->kxd = d->kya = d->kyb = 0.0f;
  d->kxb = d->kxc = d->kyc = d->kyd = 0.6f;
  d->k_space[0] = d->k_space[1] = 0.2f;
  d->k_space[2] = d->k_space[3] = 0.6f;
  d->k_apply = 0;
  d->enlarge_x = d->enlarge_y = 0.0f;
  d->flip = 0;
  d->angle = M_PI / 180.0 * p->angle;

  // image flip
  //d->flags = (p->ch < 0 ? FLAG_FLIP_VERTICAL : 0) | (p->cw < 0 ? FLAG_FLIP_HORIZONTAL : 0);
  d->crop_auto = p->crop_auto;

  printf("\n\ncommit_params(): p->k_type=%d\n", p->k_type);
  // keystones values computation
  if(p->k_type == 4)
  {
    // this is for old keystoning
    d->k_apply = 0;
    d->all_off = 1;
    if(fabsf(p->k_h) >= .0001) d->all_off = 0;
    if(p->k_h >= -1.0 && p->k_h <= 1.0)
      d->ki_h = p->k_h;
    else
      d->ki_h = 0.0f;
    if(fabsf(p->k_v) >= .0001) d->all_off = 0;
    if(p->k_v >= -1.0 && p->k_v <= 1.0)
      d->ki_v = p->k_v;
    else
      d->ki_v = 0.0f;
  }
  else if(p->k_type >= 0 && p->k_apply == 1)
  {
    // we reset old keystoning values
    d->ki_h = d->ki_v = 0;
    d->kxa = p->kxa;
    d->kxb = p->kxb;
    d->kxc = p->kxc;
    d->kxd = p->kxd;
    d->kya = p->kya;
    d->kyb = p->kyb;
    d->kyc = p->kyc;
    d->kyd = p->kyd;
    printf("commit_params(): %f %f %f %f %f %f %f %f \n", d->kxa, d->kxb, d->kxc, d->kxd, d->kya, d->kyb, d->kyc, d->kyd);
    // we adjust the points if the keystoning is not in "full" mode
    if(p->k_type == 1) // we want horizontal points to be aligned
    {
      // line equations parameters
      float a1 = (d->kxd - d->kxa) / (float)(d->kyd - d->kya);
      float b1 = d->kxa - a1 * d->kya;
      float a2 = (d->kxc - d->kxb) / (float)(d->kyc - d->kyb);
      float b2 = d->kxb - a2 * d->kyb;

      if(d->kya > d->kyb)
      {
        // we move kya to the level of kyb
        d->kya = d->kyb;
        d->kxa = a1 * d->kya + b1;
      }
      else
      {
        // we move kyb to the level of kya
        d->kyb = d->kya;
        d->kxb = a2 * d->kyb + b2;
      }

      if(d->kyc > d->kyd)
      {
        // we move kyd to the level of kyc
        d->kyd = d->kyc;
        d->kxd = a1 * d->kyd + b1;
      }
      else
      {
        // we move kyc to the level of kyd
        d->kyc = d->kyd;
        d->kxc = a2 * d->kyc + b2;
      }
    }
    else if(p->k_type == 2) // we want vertical points to be aligned
    {
      // line equations parameters
      float a1 = (d->kyb - d->kya) / (float)(d->kxb - d->kxa);
      float b1 = d->kya - a1 * d->kxa;
      float a2 = (d->kyc - d->kyd) / (float)(d->kxc - d->kxd);
      float b2 = d->kyd - a2 * d->kxd;

      if(d->kxa > d->kxd)
      {
        // we move kxa to the level of kxd
        d->kxa = d->kxd;
        d->kya = a1 * d->kxa + b1;
      }
      else
      {
        // we move kyb to the level of kya
        d->kxd = d->kxa;
        d->kyd = a2 * d->kxd + b2;
      }

      if(d->kxc > d->kxb)
      {
        // we move kyd to the level of kyc
        d->kxb = d->kxc;
        d->kyb = a1 * d->kxb + b1;
      }
      else
      {
        // we move kyc to the level of kyd
        d->kxc = d->kxb;
        d->kyc = a2 * d->kxc + b2;
      }
    }
    d->k_space[0] = fabsf((d->kxa + d->kxd) / 2.0f);
    d->k_space[1] = fabsf((d->kya + d->kyb) / 2.0f);
    d->k_space[2] = fabsf((d->kxb + d->kxc) / 2.0f) - d->k_space[0];
    d->k_space[3] = fabsf((d->kyc + d->kyd) / 2.0f) - d->k_space[1];
    d->kxb = d->kxb - d->kxa;
    d->kxc = d->kxc - d->kxa;
    d->kxd = d->kxd - d->kxa;
    d->kyb = d->kyb - d->kya;
    d->kyc = d->kyc - d->kya;
    d->kyd = d->kyd - d->kya;
    keystone_get_matrix(d->k_space, d->kxa, d->kxb, d->kxc, d->kxd, d->kya, d->kyb, d->kyc, d->kyd, &d->a,
                        &d->b, &d->d, &d->e, &d->g, &d->h);

    d->k_apply = 1;
    d->all_off = 0;
    d->crop_auto = 0;
  }
  else if(p->k_type == 0)
  {
    d->all_off = 1;
    d->k_apply = 0;
  }
  else
  {
    d->all_off = 1;
    d->k_apply = 0;
  }
  printf("commit_params(): %f %f %f %f %f %f %f %f %f\n", d->a, d->b, d->d, d->e, d->g, d->h, d->kxa, d->kya);

  /*
  if(gui_has_focus(self))
  {
    d->cx = 0.0f;
    d->cy = 0.0f;
    d->cw = 1.0f;
    d->ch = 1.0f;
  }
  else
  {
    d->cx = p->cx;
    d->cy = p->cy;
    d->cw = fabsf(p->cw);
    d->ch = fabsf(p->ch);
  }
  */
}

