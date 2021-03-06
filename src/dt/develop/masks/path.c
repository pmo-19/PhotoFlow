/*
    This file is part of darktable,
    copyright (c) 2011 henrik andersson.
    copyright (c) 2012 aldric renaudin.

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
/*
#include "develop/imageop.h"
#include "develop/blend.h"
#include "control/control.h"
#include "control/conf.h"
#include "develop/masks.h"
#include "common/debug.h"
*/

#include "../masks.h"

/** get the point of the path at pos t [0,1]  */
static void _path_get_XY(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float p3x,
                         float p3y, float t, float *x, float *y)
{
  float a = (1 - t) * (1 - t) * (1 - t);
  float b = 3 * t * (1 - t) * (1 - t);
  float c = 3 * t * t * (1 - t);
  float d = t * t * t;
  *x = p0x * a + p1x * b + p2x * c + p3x * d;
  *y = p0y * a + p1y * b + p2y * c + p3y * d;
}

/** get the point of the path at pos t [0,1]  AND the corresponding border point */
static void _path_border_get_XY(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float p3x,
                                float p3y, float t, float rad, float *xc, float *yc, float *xb, float *yb)
{
  // we get the point
  _path_get_XY(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y, t, xc, yc);

  // now we get derivative points
  float a = 3 * (1 - t) * (1 - t);
  float b = 3 * ((1 - t) * (1 - t) - 2 * t * (1 - t));
  float c = 3 * (2 * t * (1 - t) - t * t);
  float d = 3 * t * t;

  float dx = -p0x * a + p1x * b + p2x * c + p3x * d;
  float dy = -p0y * a + p1y * b + p2y * c + p3y * d;

  // so we can have the resulting point
  if(dx == 0 && dy == 0)
  {
    *xb = -9999999;
    *yb = -9999999;
    return;
  }
  float l = 1.0 / sqrtf(dx * dx + dy * dy);
  *xb = (*xc) + rad * dy * l;
  *yb = (*yc) - rad * dx * l;
}

/** get feather extremity from the control point n°2 */
/** the values should be in orthonormal space */
static void _path_ctrl2_to_feather(int ptx, int pty, int ctrlx, int ctrly, int *fx, int *fy,
                                   gboolean clockwise)
{
  if(clockwise)
  {
    *fx = ptx + ctrly - pty;
    *fy = pty + ptx - ctrlx;
  }
  else
  {
    *fx = ptx - ctrly + pty;
    *fy = pty - ptx + ctrlx;
  }
}

/** get bezier control points from feather extremity */
/** the values should be in orthonormal space */
static void _path_feather_to_ctrl(int ptx, int pty, int fx, int fy, int *ctrl1x, int *ctrl1y, int *ctrl2x,
                                  int *ctrl2y, gboolean clockwise)
{
  if(clockwise)
  {
    *ctrl2x = ptx + pty - fy;
    *ctrl2y = pty + fx - ptx;
    *ctrl1x = ptx - pty + fy;
    *ctrl1y = pty - fx + ptx;
  }
  else
  {
    *ctrl1x = ptx + pty - fy;
    *ctrl1y = pty + fx - ptx;
    *ctrl2x = ptx - pty + fy;
    *ctrl2y = pty - fx + ptx;
  }
}

/** Get the control points of a segment to match exactly a catmull-rom spline */
static void _path_catmull_to_bezier(float x1, float y1, float x2, float y2, float x3, float y3, float x4,
                                    float y4, float *bx1, float *by1, float *bx2, float *by2)
{
  *bx1 = (-x1 + 6 * x2 + x3) / 6;
  *by1 = (-y1 + 6 * y2 + y3) / 6;
  *bx2 = (x2 + 6 * x3 - x4) / 6;
  *by2 = (y2 + 6 * y3 - y4) / 6;
}

/** initialise all control points to eventually match a catmull-rom like spline */
void _path_init_ctrl_points(dt_masks_form_t *form)
{
  // if we have less that 3 points, what to do ??
  if(g_list_length(form->points) < 2) return;

  guint nb = g_list_length(form->points);
  printf("_path_init_ctrl_points(): nb=%d\n", nb);
  for(int k = 0; k < nb; k++)
  {
    dt_masks_point_path_t *point3 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k);
    // if the point as not be set manually, we redfine it
    if(point3->state & DT_MASKS_POINT_STATE_NORMAL)
    {
      // we want to get point-2, point-1, point+1, point+2
      int k1, k2, k4, k5;
      k1 = (k - 2) < 0 ? nb + (k - 2) : k - 2;
      k2 = (k - 1) < 0 ? nb - 1 : k - 1;
      k4 = (k + 1) % nb;
      k5 = (k + 2) % nb;
      dt_masks_point_path_t *point1 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k1);
      dt_masks_point_path_t *point2 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k2);
      dt_masks_point_path_t *point4 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k4);
      dt_masks_point_path_t *point5 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k5);

      printf("_path_init_ctrl_points(): point #%d=%f %f %f %f %f %f\n", k, 
             point3->corner[0], point3->corner[1],
             point3->ctrl1[0], point3->ctrl1[1],
             point3->ctrl2[0], point3->ctrl2[1]
        );

      float bx1, by1, bx2, by2;
      _path_catmull_to_bezier(point1->corner[0], point1->corner[1], point2->corner[0], point2->corner[1],
                              point3->corner[0], point3->corner[1], point4->corner[0], point4->corner[1],
                              &bx1, &by1, &bx2, &by2);
      if(point2->ctrl2[0] == -1.0) point2->ctrl2[0] = bx1;
      if(point2->ctrl2[1] == -1.0) point2->ctrl2[1] = by1;
      point3->ctrl1[0] = bx2;
      point3->ctrl1[1] = by2;
      _path_catmull_to_bezier(point2->corner[0], point2->corner[1], point3->corner[0], point3->corner[1],
                              point4->corner[0], point4->corner[1], point5->corner[0], point5->corner[1],
                              &bx1, &by1, &bx2, &by2);
      if(point4->ctrl1[0] == -1.0) point4->ctrl1[0] = bx2;
      if(point4->ctrl1[1] == -1.0) point4->ctrl1[1] = by2;
      point3->ctrl2[0] = bx1;
      point3->ctrl2[1] = by1;
    }
  }
}

static gboolean _path_is_clockwise(dt_masks_form_t *form)
{
  if(g_list_length(form->points) > 2)
  {
    float sum = 0.0f;
    guint nb = g_list_length(form->points);
    for(int k = 0; k < nb; k++)
    {
      int k2 = (k + 1) % nb;
      dt_masks_point_path_t *point1 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k);
      dt_masks_point_path_t *point2 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k2);
      // edge k
      sum += (point2->corner[0] - point1->corner[0]) * (point2->corner[1] + point1->corner[1]);
    }
    return (sum < 0);
  }
  // return dummy answer
  return TRUE;
}

/** fill eventual gaps between 2 points with a line */
static int _path_fill_gaps(int lastx, int lasty, int x, int y, dt_masks_dynbuf_t *points)
{
  dt_masks_dynbuf_reset(points);
  dt_masks_dynbuf_add(points, x);
  dt_masks_dynbuf_add(points, y);
  if(lastx == -999999) return 1;
  // now we want to be sure everything is continuous
  if(x - lastx > 1)
  {
    for(int j = x - 1; j > lastx; j--)
    {
      int yyy = (j - lastx) * (y - lasty) / (float)(x - lastx) + lasty;
      int lasty2 = dt_masks_dynbuf_get(points, -1);
      if(lasty2 - yyy > 1)
      {
        for(int jj = lasty2 + 1; jj < yyy; jj++)
        {
          dt_masks_dynbuf_add(points, j);
          dt_masks_dynbuf_add(points, jj);
        }
      }
      else if(lasty2 - yyy < -1)
      {
        for(int jj = lasty2 - 1; jj > yyy; jj--)
        {
          dt_masks_dynbuf_add(points, j);
          dt_masks_dynbuf_add(points, jj);
        }
      }
      dt_masks_dynbuf_add(points, j);
      dt_masks_dynbuf_add(points, yyy);
    }
  }
  else if(x - lastx < -1)
  {
    for(int j = x + 1; j < lastx; j++)
    {
      int yyy = (j - lastx) * (y - lasty) / (float)(x - lastx) + lasty;
      int lasty2 = dt_masks_dynbuf_get(points, -1);
      if(lasty2 - yyy > 1)
      {
        for(int jj = lasty2 + 1; jj < yyy; jj++)
        {
          dt_masks_dynbuf_add(points, j);
          dt_masks_dynbuf_add(points, jj);
        }
      }
      else if(lasty2 - yyy < -1)
      {
        for(int jj = lasty2 - 1; jj > yyy; jj--)
        {
          dt_masks_dynbuf_add(points, j);
          dt_masks_dynbuf_add(points, jj);
        }
      }
      dt_masks_dynbuf_add(points, j);
      dt_masks_dynbuf_add(points, yyy);
    }
  }
  return 1;
}

/** fill the gap between 2 points with an arc of circle */
/** this function is here because we can have gap in border, esp. if the corner is very sharp */
static void _path_points_recurs_border_gaps(float *cmax, float *bmin, float *bmin2, float *bmax, dt_masks_dynbuf_t *dpoints,
                                            dt_masks_dynbuf_t *dborder, gboolean clockwise)
{
  printf("_path_points_recurs_border_gaps() called.\n");
  // we want to find the start and end angles
  double a1 = atan2(bmin[1] - cmax[1], bmin[0] - cmax[0]);
  double a2 = atan2(bmax[1] - cmax[1], bmax[0] - cmax[0]);
  if(a1 == a2) return;

  // we have to be sure that we turn in the correct direction
  if(a2 < a1 && clockwise)
  {
    a2 += 2 * M_PI;
  }
  if(a2 > a1 && !clockwise)
  {
    a1 += 2 * M_PI;
  }

  // we determine start and end radius too
  float r1 = sqrtf((bmin[1] - cmax[1]) * (bmin[1] - cmax[1]) + (bmin[0] - cmax[0]) * (bmin[0] - cmax[0]));
  float r2 = sqrtf((bmax[1] - cmax[1]) * (bmax[1] - cmax[1]) + (bmax[0] - cmax[0]) * (bmax[0] - cmax[0]));

  // and the max length of the circle arc
  int l;
  if(a2 > a1)
    l = (a2 - a1) * fmaxf(r1, r2);
  else
    l = (a1 - a2) * fmaxf(r1, r2);
  if(l < 2) return;

  // and now we add the points
  float incra = (a2 - a1) / l;
  float incrr = (r2 - r1) / l;
  float rr = r1 + incrr;
  float aa = a1 + incra;
  for(int i = 1; i < l; i++)
  {
    dt_masks_dynbuf_add(dpoints, cmax[0]);
    dt_masks_dynbuf_add(dpoints, cmax[1]);
    if(dborder) dt_masks_dynbuf_add(dborder, cmax[0] + rr * cosf(aa));
    if(dborder) dt_masks_dynbuf_add(dborder, cmax[1] + rr * sinf(aa));
    rr += incrr;
    aa += incra;
  }
}

/** recursive function to get all points of the path AND all point of the border */
/** the function take care to avoid big gaps between points */
static void _path_points_recurs(float *p1, float *p2, double tmin, double tmax, float *path_min,
                                float *path_max, float *border_min, float *border_max, float *rpath,
                                float *rborder, dt_masks_dynbuf_t *dpoints, dt_masks_dynbuf_t *dborder,
                                int withborder)
{
  // we calculate points if needed
  if(path_min[0] == -99999)
  {
    _path_border_get_XY(p1[0], p1[1], p1[2], p1[3], p2[2], p2[3], p2[0], p2[1], tmin,
                        p1[4] + (p2[4] - p1[4]) * tmin * tmin * (3.0 - 2.0 * tmin), path_min, path_min + 1,
                        border_min, border_min + 1);
  }
  if(path_max[0] == -99999)
  {
    _path_border_get_XY(p1[0], p1[1], p1[2], p1[3], p2[2], p2[3], p2[0], p2[1], tmax,
                        p1[4] + (p2[4] - p1[4]) * tmax * tmax * (3.0 - 2.0 * tmax), path_max, path_max + 1,
                        border_max, border_max + 1);
  }
  // are the points near ?
  if((tmax - tmin < 0.0001)
     || ((int)path_min[0] - (int)path_max[0] < 1 && (int)path_min[0] - (int)path_max[0] > -1
         && (int)path_min[1] - (int)path_max[1] < 1 && (int)path_min[1] - (int)path_max[1] > -1
         && (!withborder
             || ((int)border_min[0] - (int)border_max[0] < 1 && (int)border_min[0] - (int)border_max[0] > -1
                 && (int)border_min[1] - (int)border_max[1] < 1
                 && (int)border_min[1] - (int)border_max[1] > -1))))
  {
    dt_masks_dynbuf_add(dpoints, path_max[0]);
    dt_masks_dynbuf_add(dpoints, path_max[1]);
    rpath[0] = path_max[0];
    rpath[1] = path_max[1];

    if(withborder)
    {
      dt_masks_dynbuf_add(dborder, border_max[0]);
      dt_masks_dynbuf_add(dborder, border_max[1]);
      rborder[0] = border_max[0];
      rborder[1] = border_max[1];
    }
    return;
  }

  // we split in two part
  double tx = (tmin + tmax) / 2.0;
  float c[2] = { -99999, -99999 }, b[2] = { -99999, -99999 };
  float rc[2], rb[2];
  _path_points_recurs(p1, p2, tmin, tx, path_min, c, border_min, b, rc, rb, dpoints, dborder, withborder);
  _path_points_recurs(p1, p2, tx, tmax, rc, path_max, rb, border_max, rpath, rborder, dpoints, dborder, withborder);
}

/** find all self intersections in a path */
static int _path_find_self_intersection(int *inter, int nb_corners, float *border, int border_count)
{
  int inter_count = 0;

  // we search extreme points in x and y
  int xmin, xmax, ymin, ymax;
  xmin = ymin = INT_MAX;
  xmax = ymax = INT_MIN;
  int posextr[4] = { -1 }; // xmin,xmax,ymin,ymax

  for(int i = nb_corners * 3; i < border_count; i++)
  {
    if(border[i * 2] < -999999 || border[i * 2 + 1] < -999999)
    {
      border[i * 2] = border[i * 2 - 2];
      border[i * 2 + 1] = border[i * 2 - 1];
    }
    if(xmin > border[i * 2])
    {
      xmin = border[i * 2];
      posextr[0] = i;
    }
    if(xmax < border[i * 2])
    {
      xmax = border[i * 2];
      posextr[1] = i;
    }
    if(ymin > border[i * 2 + 1])
    {
      ymin = border[i * 2 + 1];
      posextr[2] = i;
    }
    if(ymax < border[i * 2 + 1])
    {
      ymax = border[i * 2 + 1];
      posextr[3] = i;
    }
  }
  xmin -= 1, ymin -= 1;
  xmax += 1, ymax += 1;
  const int hb = ymax - ymin;
  const int wb = xmax - xmin;

  // we allocate the buffer
  const int ss = hb * wb;
  if(ss < 10) return 0;

  int *binter = calloc(ss, sizeof(int));
  if(binter == NULL) return 0;

  dt_masks_dynbuf_t *extra = dt_masks_dynbuf_init(100000, "path extra");
  if(extra == NULL)
  {
    free(binter);
    return 0;
  }

  int lastx = border[(posextr[1] - 1) * 2];
  int lasty = border[(posextr[1] - 1) * 2 + 1];

  for(int ii = nb_corners * 3; ii < border_count; ii++)
  {
    // we want to loop from one border extremity
    int i = ii - nb_corners * 3 + posextr[1];
    if(i >= border_count) i = i - border_count + nb_corners * 3;

    if(inter_count >= nb_corners * 4) break;

    // we want to be sure everything is continuous
    _path_fill_gaps(lastx, lasty, border[i * 2], border[i * 2 + 1], extra);

    // we now search intersections for all the point in extra
    for(int j = dt_masks_dynbuf_position(extra) / 2 - 1; j >= 0; j--)
    {
      int xx = (dt_masks_dynbuf_buffer(extra))[j * 2];
      int yy = (dt_masks_dynbuf_buffer(extra))[j * 2 + 1];
      int v[3] = { 0 };
      v[0] = binter[(yy - ymin) * wb + (xx - xmin)];
      if(xx > xmin) v[1] = binter[(yy - ymin) * wb + (xx - xmin - 1)];
      if(yy > ymin) v[2] = binter[(yy - ymin - 1) * wb + (xx - xmin)];
      for(int k = 0; k < 3; k++)
      {
        if(v[k] > 0)
        {
          if((xx == lastx && yy == lasty) || v[k] == i - 1)
          {
            binter[(yy - ymin) * wb + (xx - xmin)] = i;
          }
          else if((i > v[k]
                   && ((posextr[0] < i || posextr[0] > v[k]) && (posextr[1] < i || posextr[1] > v[k])
                       && (posextr[2] < i || posextr[2] > v[k]) && (posextr[3] < i || posextr[3] > v[k])))
                  || (i < v[k] && posextr[0] < v[k] && posextr[0] > i && posextr[1] < v[k] && posextr[1] > i
                      && posextr[2] < v[k] && posextr[2] > i && posextr[3] < v[k] && posextr[3] > i))
          {
            if(inter_count > 0)
            {
              if((v[k] - i) * (inter[inter_count * 2 - 2] - inter[inter_count * 2 - 1]) > 0
                 && inter[inter_count * 2 - 2] >= v[k] && inter[inter_count * 2 - 1] <= i)
              {
                inter[inter_count * 2 - 2] = v[k];
                inter[inter_count * 2 - 1] = i;
              }
              else
              {
                inter[inter_count * 2] = v[k];
                inter[inter_count * 2 + 1] = i;
                inter_count++;
              }
            }
            else
            {
              inter[inter_count * 2] = v[k];
              inter[inter_count * 2 + 1] = i;
              inter_count++;
            }
          }
        }
        else
        {
          binter[(yy - ymin) * wb + (xx - xmin)] = i;
        }
      }
      lastx = xx;
      lasty = yy;
    }
  }

  dt_masks_dynbuf_free(extra);
  free(binter);

  // and we return the number of self-intersection found
  return inter_count;
}

/** get all points of the path and the border */
/** this take care of gaps and self-intersection and iop distortions */
int _path_get_points_border(//dt_develop_t *dev,
    dt_masks_form_t *form, int prio_max,
    //dt_dev_pixelpipe_t *pipe,
    float wd, float ht,
    float **points, int *points_count,
    float **border, int *border_count, int source)
{
  //float wd = pipe->iwidth, ht = pipe->iheight;

  dt_masks_dynbuf_t *dpoints = NULL, *dborder = NULL;

  *points = NULL;
  *points_count = 0;
  if(border) *border = NULL;
  if(border) *border_count = 0;

  dpoints = dt_masks_dynbuf_init(1000000, "path dpoints");
  if(dpoints == NULL) return 0;

  if(border)
  {
    dborder = dt_masks_dynbuf_init(1000000, "path dborder");
    if(dborder == NULL)
    {
      dt_masks_dynbuf_free(dpoints);
      return 0;
    }
  }

  // we store all points
  float dx, dy;
  dx = dy = 0.0f;
  guint nb = g_list_length(form->points);
  printf("_path_get_points_border(): nb=%d\n", nb);
  if(source && nb > 0)
  {
    dt_masks_point_path_t *pt = (dt_masks_point_path_t *)g_list_nth_data(form->points, 0);
    dx = (pt->corner[0] - form->source[0]) * wd;
    dy = (pt->corner[1] - form->source[1]) * ht;
  }
  for(int k = 0; k < nb; k++)
  {
    dt_masks_point_path_t *pt = (dt_masks_point_path_t *)g_list_nth_data(form->points, k);
    dt_masks_dynbuf_add(dpoints, pt->ctrl1[0] * wd - dx);
    dt_masks_dynbuf_add(dpoints, pt->ctrl1[1] * ht - dy);
    dt_masks_dynbuf_add(dpoints, pt->corner[0] * wd - dx);
    dt_masks_dynbuf_add(dpoints, pt->corner[1] * ht - dy);
    dt_masks_dynbuf_add(dpoints, pt->ctrl2[0] * wd - dx);
    dt_masks_dynbuf_add(dpoints, pt->ctrl2[1] * ht - dy);

    printf("_path_get_points_border(): point #%d=%f %f %f %f %f %f\n", k, 
           pt->corner[0], pt->corner[1],
           pt->ctrl1[0], pt->ctrl1[1],
           pt->ctrl2[0], pt->ctrl2[1]
      );
  }
  // for the border, we store value too
  if(dborder)
  {
    for(int k = 0; k < nb; k++)
    {
      dt_masks_dynbuf_add(dborder, 0.0f);
      dt_masks_dynbuf_add(dborder, 0.0f);
      dt_masks_dynbuf_add(dborder, 0.0f);
      dt_masks_dynbuf_add(dborder, 0.0f);
      dt_masks_dynbuf_add(dborder, 0.0f);
      dt_masks_dynbuf_add(dborder, 0.0f);
    }
  }

  float border_init[6 * nb];
  int cw = _path_is_clockwise(form);
  if(cw == 0) cw = -1;

  // we render all segments
  for(int k = 0; k < nb; k++)
  {
    int pb = dborder ? dt_masks_dynbuf_position(dborder) : 0;
    border_init[k * 6 + 2] = -pb;
    int k2 = (k + 1) % nb;
    int k3 = (k + 2) % nb;
    dt_masks_point_path_t *point1 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k);
    dt_masks_point_path_t *point2 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k2);
    dt_masks_point_path_t *point3 = (dt_masks_point_path_t *)g_list_nth_data(form->points, k3);
    float p1[5] = { point1->corner[0] * wd - dx, point1->corner[1] * ht - dy, point1->ctrl2[0] * wd - dx,
                    point1->ctrl2[1] * ht - dy, cw * point1->border[1] * MIN(wd, ht) };
    float p2[5] = { point2->corner[0] * wd - dx, point2->corner[1] * ht - dy, point2->ctrl1[0] * wd - dx,
                    point2->ctrl1[1] * ht - dy, cw * point2->border[0] * MIN(wd, ht) };
    float p3[5] = { point2->corner[0] * wd - dx, point2->corner[1] * ht - dy, point2->ctrl2[0] * wd - dx,
                    point2->ctrl2[1] * ht - dy, cw * point2->border[1] * MIN(wd, ht) };
    float p4[5] = { point3->corner[0] * wd - dx, point3->corner[1] * ht - dy, point3->ctrl1[0] * wd - dx,
                    point3->ctrl1[1] * ht - dy, cw * point3->border[0] * MIN(wd, ht) };

    // and we determine all points by recursion (to be sure the distance between 2 points is <=1)
    float rc[2], rb[2];
    float bmin[2] = { -99999, -99999 };
    float bmax[2] = { -99999, -99999 };
    float cmin[2] = { -99999, -99999 };
    float cmax[2] = { -99999, -99999 };

    _path_points_recurs(p1, p2, 0.0, 1.0, cmin, cmax, bmin, bmax, rc, rb, dpoints, dborder, border && (nb >= 3));

    // we check gaps in the border (sharp edges)
    if(dborder && (fabs(dt_masks_dynbuf_get(dborder, -2) - rb[0]) > 1.0f ||
                   fabs(dt_masks_dynbuf_get(dborder, -1) - rb[1]) > 1.0f))
    {
      bmin[0] = dt_masks_dynbuf_get(dborder, -2);
      bmin[1] = dt_masks_dynbuf_get(dborder, -1);
    }

    dt_masks_dynbuf_add(dpoints, rc[0]);
    dt_masks_dynbuf_add(dpoints, rc[1]);
    
    border_init[k * 6 + 4] = dborder ? -dt_masks_dynbuf_position(dborder) : 0;

    if(dborder)
    {
      if(rb[0] == -9999999.0f)
      {
        if(dt_masks_dynbuf_get(dborder, - 2) == -9999999.0f)
        {
          dt_masks_dynbuf_set(dborder, -2, dt_masks_dynbuf_get(dborder, -4));
          dt_masks_dynbuf_set(dborder, -1, dt_masks_dynbuf_get(dborder, -3));
        }
        rb[0] = dt_masks_dynbuf_get(dborder, -2);
        rb[1] = dt_masks_dynbuf_get(dborder, -1);
      }
      dt_masks_dynbuf_add(dborder, rb[0]);
      dt_masks_dynbuf_add(dborder, rb[1]);

      (dt_masks_dynbuf_buffer(dborder))[k * 6] = border_init[k * 6] = (dt_masks_dynbuf_buffer(dborder))[pb];
      (dt_masks_dynbuf_buffer(dborder))[k * 6 + 1] = border_init[k * 6 + 1] = (dt_masks_dynbuf_buffer(dborder))[pb + 1];
    }

    // we first want to be sure that there are no gaps in border
    if(dborder && nb >= 3)
    {
      // we get the next point (start of the next segment)
      _path_border_get_XY(p3[0], p3[1], p3[2], p3[3], p4[2], p4[3], p4[0], p4[1], 0, p3[4], cmin, cmin + 1,
                          bmax, bmax + 1);
      if(bmax[0] == -9999999.0f)
      {
        _path_border_get_XY(p3[0], p3[1], p3[2], p3[3], p4[2], p4[3], p4[0], p4[1], 0.0001, p3[4], cmin,
                            cmin + 1, bmax, bmax + 1);
      }
      if(bmax[0] - rb[0] > 1 || bmax[0] - rb[0] < -1 || bmax[1] - rb[1] > 1 || bmax[1] - rb[1] < -1)
      {
        float bmin2[2] = { dt_masks_dynbuf_get(dborder, -22), dt_masks_dynbuf_get(dborder, -21) };
        _path_points_recurs_border_gaps(rc, rb, bmin2, bmax, dpoints, dborder, _path_is_clockwise(form));
      }
    }
  }

  *points_count = dt_masks_dynbuf_position(dpoints) / 2;
  *points = dt_masks_dynbuf_harvest(dpoints);
  dt_masks_dynbuf_free(dpoints);

  printf("_path_get_points_border(): points_count=%d\n", (int)(*points_count));

  if(dborder)
  {
    *border_count = dt_masks_dynbuf_position(dborder) / 2;
    *border = dt_masks_dynbuf_harvest(dborder);
    dt_masks_dynbuf_free(dborder);
  }

  // we don't want the border to self-intersect
  int intersections[nb * 8];
  int inter_count = 0;
  if(border)
  {
    inter_count = _path_find_self_intersection(intersections, nb, *border, *border_count);
  }

  //return 1;

  /*
  // and we transform them with all distorted modules
  if(dt_dev_distort_transform_plus(dev, pipe, 0, prio_max, *points, *points_count))
  {
    if(!border || dt_dev_distort_transform_plus(dev, pipe, 0, prio_max, *border, *border_count))
    {
    */
      if(border)
      {
        // we don't want to copy the falloff points
        for(int k = 0; k < nb; k++)
          for(int i = 2; i < 6; i++) (*border)[k * 6 + i] = border_init[k * 6 + i];

        // now we want to write the skipping zones
        for(int i = 0; i < inter_count; i++)
        {
          int v = intersections[i * 2];
          int w = intersections[i * 2 + 1];
          if(v <= w)
          {
            (*border)[v * 2] = -999999;
            (*border)[v * 2 + 1] = w;
          }
          else
          {
            if(w > nb * 3)
            {
              if((*border)[nb * 6] == -999999)
                (*border)[nb * 6 + 1] = MAX((*border)[nb * 6 + 1], w);
              else
                (*border)[nb * 6 + 1] = w;
              (*border)[nb * 6] = -999999;
            }
            (*border)[v * 2] = -999999;
            (*border)[v * 2 + 1] = -999999;
          }
        }
      }
      return 1;
      /*
    }
  }
*/

  // if we failed, then free all and return
  free(*points);
  *points = NULL;
  *points_count = 0;
  if(border) free(*border);
  if(border) *border = NULL;
  if(border) *border_count = 0;
  return 0;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
