/*
 * Copyright © 2020 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Matthias Clasen <mclasen@redhat.com>
 */

#include "gskpathprivate.h"

#include "gskpathbuilder.h"

#include "gskstrokeprivate.h"
#include "gskcurveprivate.h"
#include "gskpathdashprivate.h"
#include "gskpathopprivate.h"

#define STROKE_DEBUG

/* {{{ General utilities */

#define RAD_TO_DEG(r) ((r)*180.0/M_PI)
#define DEG_TO_RAD(d) ((d)*M_PI/180.0)

static inline void
_sincosf (float  angle,
          float *out_s,
          float *out_c)
{
#ifdef HAVE_SINCOSF
      sincosf (angle, out_s, out_c);
#else
      *out_s = sinf (angle);
      *out_c = cosf (angle);
#endif
}

static int
cmpfloat (const void *p1, const void *p2)
{
  const float *f1 = p1;
  const float *f2 = p2;
  return *f1 < *f2 ? -1 : (*f1 > *f2 ? 1 : 0);
}

/* }}} */
/* {{{ graphene utilities */

static void
get_tangent (const graphene_point_t *p0,
             const graphene_point_t *p1,
             graphene_vec2_t        *t)
{
  graphene_vec2_init (t, p1->x - p0->x, p1->y - p0->y);
  graphene_vec2_normalize (t, t);
}

static void
get_normal (const graphene_point_t *p0,
            const graphene_point_t *p1,
            graphene_vec2_t        *n)
{
  graphene_vec2_init (n, p0->y - p1->y, p1->x - p0->x);
  graphene_vec2_normalize (n, n);
}

/* Return the angle between t1 and t2 in radians, such that
 * 0 means straight continuation
 * < 0 means right turn
 * > 0 means left turn
 */
static float
angle_between (const graphene_vec2_t *t1,
               const graphene_vec2_t *t2)
{
  float angle = atan2 (graphene_vec2_get_y (t2), graphene_vec2_get_x (t2))
                - atan2 (graphene_vec2_get_y (t1), graphene_vec2_get_x (t1));

  if (angle > M_PI)
    angle -= 2 * M_PI;
  if (angle < - M_PI)
    angle += 2 * M_PI;

  return angle;
}

static float
angle_between_points (const graphene_point_t *c,
                      const graphene_point_t *a,
                      const graphene_point_t *b)
{
  graphene_vec2_t t1, t2;

  graphene_vec2_init (&t1, a->x - c->x, a->y - c->y);
  graphene_vec2_init (&t2, b->x - c->x, b->y - c->y);

  return angle_between (&t1, &t2);
}

static void
rotate_vector (const graphene_vec2_t *t,
               float                  alpha,
               graphene_vec2_t       *t2)
{
  float x, y, s, c;

  x = graphene_vec2_get_x (t);
  y = graphene_vec2_get_y (t);
  _sincosf (alpha, &s, &c);
  graphene_vec2_init (t2, c*x + s*y, c*y - s * x);
}

/* Set p to the intersection of the lines a + t * ab * and
 * c + s * cd. Return 1 if the lines intersect, 0 otherwise.
 */
static int
line_intersect (const graphene_point_t *a,
                const graphene_vec2_t  *ab,
                const graphene_point_t *c,
                const graphene_vec2_t  *cd,
                graphene_point_t       *p)
{
  float a1 = graphene_vec2_get_y (ab);
  float b1 = - graphene_vec2_get_x (ab);
  float c1 = a1 * a->x + b1 * a->y;

  float a2 = graphene_vec2_get_y (cd);
  float b2 = - graphene_vec2_get_x (cd);
  float c2 = a2 * c->x + b2 * c->y;

  float det = a1 * b2 - a2 * b1;

  if (fabs (det) <= 0.001)
    return 0;

  p->x = (b2 * c1 - b1 * c2) / det;
  p->y = (a1 * c2 - a2 * c1) / det;

  return 1;
}

static float
line_point_dist (const graphene_point_t *a,
                 const graphene_vec2_t  *n,
                 const graphene_point_t *q)
{
  graphene_vec2_t t;

  graphene_vec2_init (&t, q->x - a->x, q->y - a->y);
  return graphene_vec2_dot (n, &t);
}

static void
line_point (const graphene_point_t *a,
            const graphene_vec2_t  *n,
            float                   t,
            graphene_point_t       *q)
{
  q->x = a->x + t * graphene_vec2_get_x (n);
  q->y = a->y + t * graphene_vec2_get_y (n);
}

static void
rotate_point (const graphene_point_t *z,
              const graphene_point_t *p,
              float                   angle,
              graphene_point_t       *q)
{
  float x, y;
  float s, c;

  x = p->x - z->x;
  y = p->y - z->y;
  _sincosf (angle, &s, &c);

  graphene_point_init (q, z->x + x*c + y*s, z->y - x*s + y*c);
}

static int
circle_intersect (const graphene_point_t *c0,
                  float                   r0,
                  const graphene_point_t *c1,
                  float                   r1,
                  graphene_point_t        p[2])
{
  float r, R;
  float cx, cy, Cx, Cy;
  float dx, dy;
  float d;
  float x, y;
  float D;
  float angle;
  graphene_point_t p0, C;

  if (r0 < r1)
    {
      r  = r0;  R = r1;
      cx = c0->x; cy = c0->y;
      Cx = c1->x; Cy = c1->y;
    }
  else
    {
      r  = r1; R = r0;
      Cx = c0->x; Cy = c0->y;
      cx = c1->x; cy = c1->y;
    }

  dx = cx - Cx;
  dy = cy - Cy;

  d = sqrt (dx*dx + dy*dy);

  if (d < 0.0001)
    return 0;

  x = (dx / d) * R + Cx;
  y = (dy / d) * R + Cy;

  if (fabs (R + r - d) < 0.0001 ||
      fabs (R - (r + d)) < 0.0001)
    {
      graphene_point_init (&p[0], x, y);
      return 1;
    }

  if (d + r < R || R + r < d)
    return 0;

  D = (r*r - d*d - R*R) / (-2 * d * R);
  if (D >= 1)
    angle = 0;
  else if (D <= -1)
    angle = M_PI;
  else
    angle = acos (D);

  graphene_point_init (&p0, x, y);
  graphene_point_init (&C, Cx, Cy);

  rotate_point (&C, &p0, angle, &p[0]);
  rotate_point (&C, &p0, -angle, &p[1]);

  return 2;
}

static int
circle_line_intersect (const graphene_point_t *c,
                       float                   r,
                       const graphene_point_t *a,
                       const graphene_vec2_t  *t,
                       graphene_point_t        p[2])
{
  float x, y;
  float dx, dy;
  float dr2;
  float D;
  float dis;

  x = a->x - c->x;
  y = a->y - c->y;

  dx = graphene_vec2_get_x (t);
  dy = graphene_vec2_get_y (t);
  dr2 = dx * dx + dy * dy;
  D = x * (y + dy) - (x + dx) * y;
  dis = r*r*dr2 - D*D;

  if (dis < 0)
    return 0;
  else if (dis == 0)
    {
      p[0].x = c->x + D*dy / dr2;
      p[0].y = c->y - D*dx / dr2;
      return 1;
    }
  else
    {
      int sdy = dy < 0 ? -1 : 1;
      p[0].x = c->x + (D*dy + sdy*dx*sqrt (dis)) / dr2;
      p[0].y = c->y + (-D*dx + fabs (dy)*sqrt (dis)) / dr2;
      p[1].x = c->x + (D*dy - sdy*dx*sqrt (dis)) / dr2;
      p[1].y = c->y + (-D*dx - fabs (dy)*sqrt (dis)) / dr2;
      return 2;
    }
}

static void
find_closest_point (const graphene_point_t *p,
                    int                     n,
                    const graphene_point_t *c,
                    graphene_point_t       *d)
{
  float dist;

  *d = p[0];
  dist = graphene_point_distance (&p[0], c, NULL, NULL);
  for (int i = 1; i < n; i++)
    {
      float dist2 = graphene_point_distance (&p[i], c, NULL, NULL);
      if (dist2 < dist)
        {
          dist = dist2;
          *d = p[i];
        }
    }
}

/* Find the circle through p0 and p1 with tangent t
 * at p1, and set c to its center.
 */
static int
find_tangent_circle (const graphene_point_t *p0,
                     const graphene_point_t *p1,
                     const graphene_vec2_t  *t,
                     graphene_point_t       *c)
{
  graphene_vec2_t n1, n2;
  graphene_point_t p2;

  graphene_vec2_init (&n1, - graphene_vec2_get_y (t), graphene_vec2_get_x (t));

  get_normal (p0, p1, &n2);

  p2.x = (p0->x + p1->x) / 2;
  p2.y = (p0->y + p1->y) / 2;

  return line_intersect (p1, &n1, &p2, &n2, c);
}

/* }}} */
/* {{{ GskPathBuilder utilities */

static void
path_builder_move_to_point (GskPathBuilder         *builder,
                            const graphene_point_t *point)
{
  gsk_path_builder_move_to (builder, point->x, point->y);
}

static void
path_builder_line_to_point (GskPathBuilder         *builder,
                            const graphene_point_t *point)
{
  gsk_path_builder_line_to (builder, point->x, point->y);
}

/* Assumes that the current point of the builder is
 * the start point of the curve
 */
static void
path_builder_add_curve (GskPathBuilder *builder,
                        const GskCurve *curve)
{
  const graphene_point_t *p;

  switch (curve->op)
    {
    case GSK_PATH_LINE:
      p = curve->line.points;
      gsk_path_builder_line_to (builder, p[1].x, p[1].y);
      break;

    case GSK_PATH_QUAD:
      p = curve->quad.points;
      gsk_path_builder_quad_to (builder, p[1].x, p[1].y,
                                         p[2].x, p[2].y);
      break;

    case GSK_PATH_CUBIC:
      p = curve->cubic.points;
      gsk_path_builder_cubic_to (builder, p[1].x, p[1].y,
                                          p[2].x, p[2].y,
                                          p[3].x, p[3].y);
      break;

    case GSK_PATH_CONIC:
      p = curve->conic.points;
      gsk_path_builder_conic_to (builder, p[1].x, p[1].y,
                                          p[3].x, p[3].y,
                                          p[2].x);
      break;

    case GSK_PATH_MOVE:
    case GSK_PATH_CLOSE:
    default:
      g_assert_not_reached ();
    }
}

static void
path_builder_add_curves (GskPathBuilder *builder,
                         GArray         *curves)
{
  for (unsigned int i = 0; i < curves->len; i++)
    {
      GskCurve *c = &g_array_index (curves, GskCurve, i);
      path_builder_add_curve (builder, c);
    }
}

static gboolean
add_reverse_op (GskPathOperation        op,
                const graphene_point_t *pts,
                gsize                   n_pts,
                float                   weight,
                gpointer                user_data)
{
  GskCurve c;
  GskCurve *curve;
  GList **ops = user_data;

  if (op == GSK_PATH_MOVE)
    return TRUE;

  gsk_curve_init_foreach (&c, op, pts, n_pts, weight);
  curve = g_new0 (GskCurve, 1);
  gsk_curve_reverse (&c, curve);

  *ops = g_list_prepend (*ops, curve);

  return TRUE;
}

static void
path_builder_add_reverse_path (GskPathBuilder *builder,
                               GskPath        *path)
{
  GList *l, *ops;

  ops = NULL;
  gsk_path_foreach (path, -1, add_reverse_op, &ops);
  for (l = ops; l; l = l->next)
    {
      GskCurve *curve = l->data;
      path_builder_add_curve (builder, curve);
    }
  g_list_free_full (ops, g_free);
}

/* Draw a circular arc from the current point to e,
 * around c
 */
static gboolean
path_builder_arc_to (GskPathBuilder         *builder,
                     const graphene_point_t *c,
                     const graphene_point_t *e)
{
  const graphene_point_t *s;
  graphene_vec2_t ts, te;
  graphene_point_t p;
  float a, w;

  s = gsk_path_builder_get_current_point (builder);

  get_normal (c, s, &ts);
  get_normal (c, e, &te);

  if (!line_intersect (s, &ts, e, &te, &p))
    return FALSE;

  a = angle_between_points (&p, s, e);
  w = fabs (sin (a / 2));

  gsk_path_builder_conic_to (builder, p.x, p.y, e->x, e->y, w);
  return TRUE;
}

/* }}} */
/* {{{ GskCurve utilities */

static gboolean
curve_is_ignorable (const GskCurve *curve)
{
  if (curve->op == GSK_PATH_CUBIC)
    {
      const graphene_point_t *pts = curve->cubic.points;

      if (graphene_point_near (&pts[0], &pts[1], 0.001) &&
          graphene_point_near (&pts[1], &pts[2], 0.001) &&
          graphene_point_near (&pts[2], &pts[3], 0.001))
        return TRUE;
    }
  else if (curve->op == GSK_PATH_CONIC)
    {
      const graphene_point_t *pts = curve->conic.points;

      if (graphene_point_near (&pts[0], &pts[1], 0.001) &&
          graphene_point_near (&pts[1], &pts[3], 0.001))
        return TRUE;
    }
  else if (curve->op == GSK_PATH_QUAD)
    {
      const graphene_point_t *pts = curve->quad.points;

      if (graphene_point_near (&pts[0], &pts[1], 0.001) &&
          graphene_point_near (&pts[1], &pts[2], 0.001))
        return TRUE;
    }
  else if (curve->op == GSK_PATH_LINE)
    {
      const graphene_point_t *pts = curve->line.points;

      if (graphene_point_near (&pts[0], &pts[1], 0.001))
        return TRUE;
    }

  return FALSE;
}

static gboolean
cubic_is_simple (const GskCurve *curve)
{
  const graphene_point_t *pts = curve->cubic.points;
  float a1, a2, s;
  graphene_vec2_t t1, t2, t3;

  if (!graphene_point_near (&pts[0], &pts[1], 0.001) &&
      !graphene_point_near (&pts[1], &pts[2], 0.001) &&
      !graphene_point_near (&pts[2], &pts[3], 0.001))
    {
      get_tangent (&pts[0], &pts[1], &t1);
      get_tangent (&pts[1], &pts[2], &t2);
      get_tangent (&pts[2], &pts[3], &t3);
      a1 = angle_between (&t1, &t2);
      a2 = angle_between (&t2, &t3);

      if ((a1 < 0 && a2 > 0) || (a1 > 0 && a2 < 0))
        return FALSE;
    }

  gsk_curve_get_start_tangent (curve, &t1);
  gsk_curve_get_end_tangent (curve, &t2);
  s = graphene_vec2_dot (&t1, &t2);

  if (fabs (acos (s)) >= M_PI / 3.f)
    return FALSE;

  return TRUE;
}

static gboolean
conic_is_simple (const GskCurve *curve)
{
  const graphene_point_t *pts = curve->conic.points;
  graphene_vec2_t n1, n2;
  float s;

  get_normal (&pts[0], &pts[1], &n1);
  get_normal (&pts[1], &pts[3], &n2);

  s = graphene_vec2_dot (&n1, &n2);

  if (fabs (acos (s)) >= M_PI / 3.f)
    return FALSE;

  return TRUE;
}

static gboolean
conic_is_degenerate (const GskCurve *curve)
{
  if (curve->op == GSK_PATH_CONIC)
    {
      const graphene_point_t *pts = curve->conic.points;
      float a;
      graphene_vec2_t t1, t2;

      get_tangent (&pts[0], &pts[1], &t1);
      get_tangent (&pts[1], &pts[3], &t2);
      a = angle_between (&t1, &t2);

      if (a < 0)
        a += M_PI;

      if (fabs (a) < DEG_TO_RAD (3))
        return TRUE;
    }

  return FALSE;
}

typedef void (* AddCurveCallback) (const GskCurve *curve,
                                   gpointer        data);

static void
split_curve_by_cusps (const GskCurve   *curve,
                      AddCurveCallback  callback,
                      gpointer          data)
{
  float t[5], s;
  int n;

  if ((n = gsk_curve_get_cusps (curve, t)) > 0)
    {
      t[n++] = 0;
      t[n++] = 1;
    }

  if (n <= 2)
    callback (curve, data);
  else
    {
      qsort (t, n, sizeof (float), cmpfloat);

      s = 0;
      for (int i = 1; i < n; i++)
        {
          GskCurve c;
          if (fabs (t[i] - s) > 0.1 || i == n - 1)
            {
              gsk_curve_segment (curve, s, t[i], &c);
              callback (&c, data);
              s = t[i];
            }
        }
    }
}

/* Given two curves that share an end point,
 * find the intersection that is closest to
 * their shared endpoint, if any, and return
 * the distance. If there is no intersection,
 * return G_MAXFLOAT.
 */
static float
find_closest_intersection (const GskCurve   *c1,
                           const GskCurve   *c2,
                           graphene_point_t *p_out)
{
  graphene_point_t p[9];
  float t1[9], t2[9];
  int n;

  n = gsk_curve_intersect (c1, c2, t1, t2, p, 9);

  if (n > 1)
    {
      const graphene_point_t *end = gsk_curve_get_end_point (c1);
      float min = G_MAXFLOAT;
      int best = -1;

      for (unsigned int i = 0; i < n; i++)
        {
          float d;

          if (fabs (t2[i]) < 0.001)
            continue;

          d = graphene_point_distance (end, &p[i], NULL, NULL);
          if (d < min)
            {
              best = i;
              min = d;
            }
        }

      if (best == -1)
        return G_MAXFLOAT;

      if (p_out)
        *p_out = p[best];

      return min;
    }

  return G_MAXFLOAT;
}

/* }}} */
/* {{{ Stroke helpers */

static void
add_line_join (GskPathBuilder         *builder,
               GskLineJoin             line_join,
               float                   width,
               float                   miter_limit,
               const graphene_point_t *c,
               const GskCurve         *aa,
               const GskCurve         *bb,
               float                   angle)
{
  const graphene_point_t *a;
  const graphene_point_t *b;
  graphene_vec2_t ta;
  graphene_vec2_t tb;
  float ml, w;

  ml = MAX (miter_limit, 1);
  w = width;

  a = gsk_curve_get_end_point (aa);
  gsk_curve_get_end_tangent (aa, &ta);
  b = gsk_curve_get_start_point (bb);
  gsk_curve_get_start_tangent (bb, &tb);

again:
  switch (line_join)
    {
    case GSK_LINE_JOIN_ARCS:
      {
        float ka, kb;
        graphene_point_t ca, cb;
        graphene_point_t p[2];
        int n;

        if (line_intersect (a, &ta, b, &tb, p) == 0)
          {
            line_point (a, &ta, ml * w, &p[0]);
            line_point (b, &tb, - ml * w, &p[1]);
            path_builder_line_to_point (builder, &p[0]);
            path_builder_line_to_point (builder, &p[1]);
            path_builder_line_to_point (builder, b);
            break;
          }

        ka = gsk_curve_get_curvature (aa, 1, &ca);
        kb = gsk_curve_get_curvature (bb, 0, &cb);

        if (ka == 0 && kb == 0)
          {
            line_join = GSK_LINE_JOIN_MITER_CLIP;
            goto again;
          }

        if (ka > 1 / width || kb > 1 / width)
          {
            line_join = GSK_LINE_JOIN_ROUND;
            goto again;
          }

        if (ka != 0 && kb != 0)
          {
            float ra, rb;
            graphene_point_t p0;

            ra = fabs (1/ka);
            rb = fabs (1/kb);

            n = circle_intersect (&ca, ra, &cb, rb, p);

            if (n == 0)
              {
                graphene_vec2_t ac, bc;
                float d;
                float rr0, rr1, rr;
                int m;
                graphene_point_t cca, ccb;
                float rra, rrb;

                d = graphene_point_distance (&ca, &cb, NULL, NULL);

                if (d > ra + rb)
                  {
                    gboolean big_enough;

                    /* disjoint circles */

                    get_tangent (a, &ca, &ac);
                    get_tangent (b, &cb, &bc);

                    rr0 = 0;
                    rr1 = d;
                    big_enough = FALSE;
                    while (fabs (rr1 - rr0) > 0.001)
                      {
                        rr = (rr0 + rr1) / 2;
                        rra = ra + rr;
                        rrb = rb + rr;
                        line_point (a, &ac, rra, &cca);
                        line_point (b, &bc, rrb, &ccb);
                        m = circle_intersect (&cca, rra, &ccb, rrb, p);
                        if (m == 1)
                          {
                            rr1 = rr;
                            break;
                          }
                        if (m == 2)
                          {
                            rr1 = rr;
                            big_enough = TRUE;
                          }
                        else
                          {
                            if (big_enough)
                              rr0 = rr;
                            else
                              rr1 += d;
                          }
                      }

                    ra += rr1;
                    rb += rr1;

                    line_point (a, &ac, ra, &ca);
                    line_point (b, &bc, rb, &cb);

                    n = circle_intersect (&ca, ra, &cb, rb, p);
                  }
                else
                  {
                    /* contained circles */

                    get_tangent (a, &ca, &ac);
                    get_tangent (b, &cb, &bc);

                    rr0 = 0;
                    rr1 = rb - ra;
                    while (fabs (rr1 - rr0) > 0.001)
                      {
                        rr = (rr0 + rr1) / 2;
                        rra = ra + rr;
                        rrb = rb - rr;
                        line_point (a, &ac, rra, &cca);
                        line_point (b, &bc, rrb, &ccb);
                        m = circle_intersect (&cca, rra, &ccb, rrb, p);
                        if (m == 1)
                          {
                            rr1 = rr;
                            break;
                          }
                        if (m == 2)
                          rr1 = rr;
                        else
                          rr0 = rr;
                      }

                    ra += rr1;
                    rb -= rr1;

                    line_point (a, &ac, ra, &ca);
                    line_point (b, &bc, rb, &cb);

                    n = circle_intersect (&ca, ra, &cb, rb, p);
                  }
              }

            if (n > 0)
              {
                /* We have an intersection; connect things */
                graphene_vec2_t n0;
                graphene_point_t cm;
                float rm;
                graphene_vec2_t t;
                float alpha;
                graphene_point_t pa, pb;

                find_closest_point (p, n, c, &p0);
                get_normal (&ca, &cb, &n0);

                if (find_tangent_circle (c, &p0, &n0, &cm))
                  {
                    rm = graphene_point_distance (c, &cm, NULL, NULL);

                    alpha = angle_between_points (&cm, c, &p0);

                    /* FIXME: currently, we don't let miter_limit go below 1,
                     * to avoid dealing with bevel overlap
                     */
                    if (fabs (alpha * rm) > ml * w)
                      {
                        get_tangent (&cm, c, &t);
                        rotate_vector (&t, ml * w / rm, &t);
                        n = circle_line_intersect (&ca, ra, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pa);
                        n = circle_line_intersect (&cb, rb, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pb);

                        path_builder_arc_to (builder, &ca, &pa);
                        path_builder_line_to_point (builder, &pb);
                        path_builder_arc_to (builder, &cb, b);
                      }
                    else
                      {
                        path_builder_arc_to (builder, &ca, &p0);
                        path_builder_arc_to (builder, &cb, b);
                      }
                  }
                else
                  {
                    if (graphene_point_distance (c, &p0, NULL, NULL) > ml * w)
                      {
                        get_tangent (c, &p0, &t);
                        line_point (c, &t, ml * w, &cm);
                        graphene_vec2_init (&t, - graphene_vec2_get_y (&t), graphene_vec2_get_x (&t));

                        n = circle_line_intersect (&ca, ra, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pa);
                        n = circle_line_intersect (&cb, rb, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pb);

                        path_builder_arc_to (builder, &ca, &pa);
                        path_builder_line_to_point (builder, &pb);
                        path_builder_arc_to (builder, &cb, b);
                      }
                    else
                      {
                        path_builder_arc_to (builder, &ca, &p0);
                        path_builder_arc_to (builder, &cb, b);
                      }
                  }
              }
          }
        else if (ka != 0 && kb == 0)
          {
            float ra = fabs (1/ka);
            graphene_vec2_t ac;
            float rr0, rr1, rr, rra;
            graphene_point_t cca;
            int m;

            n = circle_line_intersect (&ca, ra, b, &tb, p);
            if (n == 0)
              {
                gboolean big_enough;

                get_tangent (a, &ca, &ac);

                rr = 0;
                rr0 = 0;
                rr1 = ra;
                big_enough = FALSE;
                while (fabs (rr1 - rr0) > 0.001)
                  {
                    rr = (rr0 + rr1) / 2;
                    rra = ra + rr;
                    line_point (a, &ac, rra, &cca);
                    m = circle_line_intersect (&cca, rra, b, &tb, p);
                    if (m == 1)
                      break;
                    if (m == 2)
                      {
                        big_enough = TRUE;
                        rr1 = rr;
                      }
                    else
                      {
                        if (big_enough)
                          rr0 = rr;
                        else
                          rr1 += ra;
                      }
                  }

                ra += rr1;
                line_point (a, &ac, ra, &ca);
                n = circle_line_intersect (&ca, ra, b, &tb, p);
              }

            if (n > 0)
              {
                graphene_point_t p0, cm;
                graphene_vec2_t n0;
                float rm;
                float alpha;

                find_closest_point (p, n, c, &p0);
                if (n == 1)
                  graphene_vec2_init_from_vec2 (&n0, &tb);
                else
                  get_tangent (&p[0], &p[1], &n0);

                if (find_tangent_circle (c, &p0, &n0, &cm))
                  {
                    rm = graphene_point_distance (c, &cm, NULL, NULL);

                    alpha = angle_between_points (&cm, c, &p0);

                    if (fabs (alpha * rm) > ml * w)
                      {
                        graphene_vec2_t t;
                        graphene_point_t pa, pb;

                        get_tangent (&cm, c, &t);
                        rotate_vector (&t, ml * w / rm, &t);
                        n = circle_line_intersect (&ca, ra, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pa);
                        n = line_intersect (b, &tb, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pb);

                        path_builder_arc_to (builder, &ca, &pa);
                        path_builder_line_to_point (builder, &pb);
                        path_builder_line_to_point (builder, b);
                      }
                    else
                      {
                        path_builder_arc_to (builder, &ca, &p0);
                        path_builder_line_to_point (builder, b);
                      }
                  }
                else
                  {
                    if (graphene_point_distance (c, &p0, NULL, NULL) > ml * w)
                      {
                        graphene_vec2_t t;
                        graphene_point_t pa, pb;

                        get_tangent (c, &p0, &t);
                        line_point (c, &t, ml * w, &cm);
                        graphene_vec2_init (&t, - graphene_vec2_get_y (&t), graphene_vec2_get_x (&t));

                        n = circle_line_intersect (&ca, ra, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pa);
                        n = line_intersect (b, &tb, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pb);

                        path_builder_arc_to (builder, &ca, &pa);
                        path_builder_line_to_point (builder, &pb);
                        path_builder_line_to_point (builder, b);
                      }
                    else
                      {
                        path_builder_arc_to (builder, &ca, &p0);
                        path_builder_line_to_point (builder, b);
                      }
                  }
              }
          }
        else if (ka == 0 && kb != 0)
          {
            float rb = fabs (1/kb);
            graphene_vec2_t bc;
            float rr0, rr1, rr, rrb;
            graphene_point_t ccb;
            int m;

            n = circle_line_intersect (&cb, rb, a, &ta, p);
            if (n == 0)
              {
                gboolean big_enough;

                get_tangent (b, &cb, &bc);

                rr = 0;
                rr0 = 0;
                rr1 = rb;
                big_enough = FALSE;
                while (fabs (rr1 - rr0) > 0.001)
                  {
                    rr = (rr0 + rr1) / 2;
                    rrb = rb + rr;
                    line_point (b, &bc, rrb, &ccb);
                    m = circle_line_intersect (&ccb, rrb, a, &ta, p);
                    if (m == 1)
                      break;
                    if (m == 2)
                      {
                        big_enough = TRUE;
                        rr1 = rr;
                      }
                    else
                      {
                        if (big_enough)
                          rr0 = rr;
                        else
                          rr1 += rb;
                      }
                  }

                rb += rr1;
                line_point (b, &bc, rb, &cb);
                n = circle_line_intersect (&cb, rb, a, &ta, p);
              }

            if (n > 0)
              {
                graphene_point_t p0, cm;
                graphene_vec2_t n0;
                float rm;
                float alpha;

                find_closest_point (p, n, c, &p0);
                if (n == 1)
                  graphene_vec2_init_from_vec2 (&n0, &tb);
                else
                  get_tangent (&p[0], &p[1], &n0);

                if (find_tangent_circle (c, &p0, &n0, &cm))
                  {
                    rm = graphene_point_distance (c, &cm, NULL, NULL);

                    alpha = angle_between_points (&cm, c, &p0);

                    if (fabs (alpha * rm) > ml * w)
                      {
                        graphene_vec2_t t;
                        graphene_point_t pa, pb;

                        get_tangent (&cm, c, &t);
                        rotate_vector (&t, ml * w / rm, &t);
                        n = line_intersect (a, &ta, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pa);
                        n = circle_line_intersect (&cb, rb, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pb);

                        path_builder_line_to_point (builder, &pa);
                        path_builder_line_to_point (builder, &pb);
                        path_builder_arc_to (builder, &cb, b);
                      }
                    else
                      {
                        path_builder_line_to_point (builder, &p0);
                        path_builder_arc_to (builder, &cb, b);
                      }
                  }
                else
                  {
                    if (graphene_point_distance (c, &p0, NULL, NULL) > ml * w)
                      {
                        graphene_vec2_t t;
                        graphene_point_t pa, pb;

                        get_tangent (c, &p0, &t);
                        line_point (c, &t, ml * w, &cm);
                        graphene_vec2_init (&t, - graphene_vec2_get_y (&t), graphene_vec2_get_x (&t));

                        n = line_intersect (a, &ta, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pa);
                        n = circle_line_intersect (&cb, rb, &cm, &t, p);
                        find_closest_point (p, n, &p0, &pb);

                        path_builder_line_to_point (builder, &pa);
                        path_builder_line_to_point (builder, &pb);
                        path_builder_arc_to (builder, &cb, b);
                      }
                    else
                      {
                        path_builder_line_to_point (builder, &p0);
                        path_builder_arc_to (builder, &cb, b);
                      }
                  }
              }
          }
      }
      break;

    case GSK_LINE_JOIN_MITER:
    case GSK_LINE_JOIN_MITER_CLIP:
      {
        graphene_point_t p;

        if (line_intersect (a, &ta, b, &tb, &p))
          {
            graphene_vec2_t tam;

            graphene_vec2_negate (&ta, &tam);

            /* Check that 1 / sin (psi / 2) <= miter_limit,
             * where psi is the angle between ta and tb
             */
            if (2 <= miter_limit * miter_limit * (1 - graphene_vec2_dot (&tam, &tb)))
              {
                path_builder_line_to_point (builder, &p);
                path_builder_line_to_point (builder, b);
              }
            else if (line_join == GSK_LINE_JOIN_MITER_CLIP)
              {
                graphene_point_t q, a1, b1;
                graphene_vec2_t n;

                q.x = (c->x + p.x) / 2;
                q.y = (c->y + p.y) / 2;
                get_normal (c, &p, &n);

                line_intersect (a, &ta, &q, &n, &a1);
                line_intersect (b, &tb, &q, &n, &b1);

                if ((line_point_dist (&q, &n, c) < 0) !=
                    (line_point_dist (&q, &n, a) < 0))
                  {
                    path_builder_line_to_point (builder, &a1);
                    path_builder_line_to_point (builder, &b1);
                    path_builder_line_to_point (builder, b);
                  }
                else
                  {
                    path_builder_line_to_point (builder, b);
                  }
              }
            else
              {
                path_builder_line_to_point (builder, b);
              }
          }
        else
          {
            path_builder_line_to_point (builder, b);
          }
      }
      break;

    case GSK_LINE_JOIN_ROUND:
      gsk_path_builder_svg_arc_to (builder,
                                   w, w,
                                   0, 0, angle > 0 ? 1 : 0,
                                   b->x, b->y);
      break;

    case GSK_LINE_JOIN_BEVEL:
      path_builder_line_to_point (builder, b);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
add_line_cap (GskPathBuilder         *builder,
              GskLineCap              line_cap,
              float                   line_width,
              const graphene_point_t *s,
              const graphene_point_t *e)
{
    switch (line_cap)
    {
    case GSK_LINE_CAP_BUTT:
      path_builder_line_to_point (builder, e);
      break;

    case GSK_LINE_CAP_ROUND:
      gsk_path_builder_svg_arc_to (builder,
                                   line_width / 2, line_width / 2,
                                   0, 1, 0,
                                   e->x, e->y);
      break;

    case GSK_LINE_CAP_SQUARE:
      {
        float cx = (s->x + e->x) / 2;
        float cy = (s->y + e->y) / 2;
        float dx = s->y - cy;
        float dy = - s->x + cx;

        gsk_path_builder_line_to (builder, s->x + dx, s->y + dy);
        gsk_path_builder_line_to (builder, e->x + dx, e->y + dy);
        path_builder_line_to_point (builder, e);
      }
      break;

    default:
      g_assert_not_reached ();
      break;
    }
}

/* }}} */
/* {{{ Segments */

/* A Segment contains a curve from the path we're operating on,
 * plus its subdivision at a possible offset.
 */

typedef struct
{
  GskCurve curve;
  GArray *c;
  float offset;
} Segment;

static void
segment_init (Segment        *segment,
              const GskCurve *curve)
{
  memset (segment, 0, sizeof (Segment));
  if (curve)
    segment->curve = *curve;
  segment->c = g_array_new (FALSE, TRUE, sizeof (GskCurve));
  segment->offset = 0;
}

static void
segment_clear (Segment *segment)
{
  g_array_unref (segment->c);
}

static void
segment_copy (Segment *src,
              Segment *dest)
{
  dest->curve = src->curve;
  dest->offset = src->offset;
  if (dest->c == NULL)
    dest->c = g_array_new (FALSE, TRUE, sizeof (GskCurve));
  else
    g_array_set_size (dest->c, 0);
  g_array_append_vals (dest->c, src->c->data, src->c->len);
}

static inline unsigned int
segment_get_n_curves (Segment *segment)
{
  return segment->c->len;
}

static inline const GskCurve *
segment_get_curve (Segment      *segment,
                   unsigned int  pos)
{
  return &g_array_index (segment->c, const GskCurve, pos);
}

static inline const GskCurve *
segment_get_first_curve (Segment *segment)
{
  return segment_get_curve (segment, 0);
}

static inline const GskCurve *
segment_get_last_curve (Segment *segment)
{
  return segment_get_curve (segment, segment->c->len - 1);
}

static void
segment_offset (Segment *segment,
                float    offset)
{
  for (unsigned int i = 0; i < segment->c->len; i++)
    {
      GskCurve *c = &g_array_index (segment->c, GskCurve, i);
      gsk_curve_offset (c, offset, c);
    }
  segment->offset += offset;
}

/* {{{ Subdivision */

#define MAX_SUBDIVISION 3

static inline void
add_curve (const GskCurve *curve,
           Segment        *segment)
{
  if (curve_is_ignorable (curve))
    return;

  g_array_append_val (segment->c, *curve);
}

static void
curve_subdivide (const GskCurve *curve,
                 unsigned int    level,
                 Segment        *segment)
{
  GskCurve c1, c2;

  if (level == 0)
    {
      add_curve (curve, segment);
      return;
    }

  if (level < MAX_SUBDIVISION && cubic_is_simple (curve))
    {
      add_curve (curve, segment);
      return;
    }

  if (level == MAX_SUBDIVISION)
    {
      float t[5] = { 0, 1, };
      int n = 2;

      n += gsk_curve_get_curvature_points (curve, t + n);
      if (n > 2)
        {
          float s;

          qsort (t, n, sizeof (float), cmpfloat);

          s = 0;
          for (int i = 1; i < n; i++)
            {
              GskCurve c;

              if (fabs (t[i] - s) > 0.1 || i == n - 1)
                {
                  gsk_curve_segment (curve, s, t[i], &c);
                  curve_subdivide (&c, level - 1, segment);
                  s = t[i];
                }
            }
          return;
        }
    }

  gsk_curve_split (curve, 0.5, &c1, &c2);
  curve_subdivide (&c1, level - 1, segment);
  curve_subdivide (&c2, level - 1, segment);
}

static void
add_degenerate_conic (const GskCurve *curve,
                      Segment        *segment)
{
  graphene_point_t p[3];
  GskCurve c;

  p[0] = *gsk_curve_get_start_point (curve);
  gsk_curve_get_point (curve, 0.5, &p[1]);
  p[2] = *gsk_curve_get_end_point (curve);

  gsk_curve_init_foreach (&c, GSK_PATH_LINE, p, 2, 0);
  add_curve (&c, segment);

  gsk_curve_init_foreach (&c, GSK_PATH_LINE, p + 1, 2, 0);
  add_curve (&c, segment);
}

static void
conic_subdivide (const GskCurve *curve,
                 int             level,
                 Segment        *segment)
{
  if (level == 0)
    {
      if (conic_is_degenerate (curve))
        add_degenerate_conic (curve, segment);
      else
        add_curve (curve, segment);
    }
  else if (level < MAX_SUBDIVISION && conic_is_simple (curve))
    {
      add_curve (curve, segment);
    }
  else
    {
      GskCurve c1, c2;

      gsk_curve_split (curve, 0.5, &c1, &c2);
      conic_subdivide (&c1, level - 1, segment);
      conic_subdivide (&c2, level - 1, segment);
    }
}

static void
segment_subdivide (Segment *segment)
{
  const GskCurve *curve = &segment->curve;

  g_assert (segment->c->len == 0);

  switch (curve->op)
    {
    case GSK_PATH_LINE:
      add_curve (curve, segment);
      break;

    case GSK_PATH_QUAD:
    case GSK_PATH_CUBIC:
      curve_subdivide (curve, MAX_SUBDIVISION, segment);
      break;

    case GSK_PATH_CONIC:
      conic_subdivide (curve, MAX_SUBDIVISION, segment);
      break;

    case GSK_PATH_MOVE:
    case GSK_PATH_CLOSE:
    default:
      g_assert_not_reached ();
    }

  if (segment->c->len == 0)
    {
      graphene_point_t p[2];
      GskCurve c;

      g_warning ("This should not happen. Expect bad strokes");
      p[0] = *gsk_curve_get_start_point (curve);
      p[1] = *gsk_curve_get_end_point (curve);
      gsk_curve_init_foreach (&c, GSK_PATH_LINE, p, 2, 0);
      g_array_append_val (segment->c, *curve);
    }
}

/* }}} */
/* {{{ Loop elision */

static gboolean
has_nontrivial_intersection (float *t1, float *t2, int n)
{
  for (int i = 0; i < n; i++)
    {
      if (fabs (t1[i]) > 0.01 && fabs (t1[i] - 1.) > 0.01 &&
          fabs (t2[i]) > 0.01 && fabs (t2[i] - 1.) > 0.01)
        return TRUE;
    }

  return FALSE;
}

static int
segments_intersect (GArray           *curves1,
                    GArray           *curves2,
                    gboolean          no_self,
                    int              *i_out,
                    int              *j_out,
                    float            *t1,
                    float            *t2,
                    graphene_point_t *p,
                    int               n)
{
  for (int i = curves1->len - 1; i >= 0; i--)
    {
      GskCurve *c1 = &g_array_index (curves1, GskCurve, i);

      for (int j = 0; j < curves2->len; j++)
        {
          GskCurve *c2 = &g_array_index (curves2, GskCurve, j);

          if (no_self && i == j)
            break;

          int count = gsk_curve_intersect (c1, c2, t1, t2, p, n);

          if (count > 0)
            {
              if (no_self && i == j + 1 && !has_nontrivial_intersection (t1, t2, n))
                continue;

              *i_out = i;
              *j_out = j;
              return count;
            }
        }
    }

  return 0;
}

static gboolean
cut_loop (GArray *curves)
{
  int i, j;
  float t1, t2;
  graphene_point_t p;

  if (segments_intersect (curves, curves, TRUE, &j, &i, &t2, &t1, &p, 1) > 0)
    {
      GskCurve *r;
      GskCurve c;

      if (i + 1 < j)
        g_array_remove_range (curves, i + 1, j - (i + 1));
      else if (i + 1 == j)
        {
          /* nothing to do */
        }
      else if (i == j)
        {
          c = g_array_index (curves, GskCurve, i);
          g_array_insert_val (curves, i + 1, c);
        }
      else
        g_assert_not_reached ();

      r = &g_array_index (curves, GskCurve, i);
      gsk_curve_split (r, t1, &c, NULL);
      *r = c;

      r = &g_array_index (curves, GskCurve, i + 1);
      gsk_curve_split (r, t2, NULL, &c);
      *r = c;

      return TRUE;
    }

  return FALSE;
}

static void
elide_self_intersections (Segment *segment)
{
  if (segment->curve.op == GSK_PATH_LINE ||
      segment->curve.op == GSK_PATH_QUAD)
    return;

  if (segment->curve.op == GSK_PATH_CUBIC)
    {
      GskCurve c;
      float t1, t2;
      graphene_point_t p;

      /* If the original curve self-intersects, assume this
       * is an intentional loop that we shouldn't cut.
       */
      c = segment->curve;
      if (gsk_curve_intersect (&c, &c, &t1, &t2, &p, 1) > 0)
        {
          return;
        }
    }

  /* FIXME: For conics and non-inflecting cubics,
   * it would be enough to check the 'inner' side.
   */
  cut_loop (segment->c);
}

/* }}} */
/* }}} */
/* {{{ Stroke implementation */

/* The general theory of operation for the stroker:
 *
 * We walk the segments of the path, subdividing and offsetting
 * each segment to the left and right, and collect the offset
 * segments in a left and a right contour.
 *
 * When a segment is too curvy, or has cusps or inflections,
 * we subdivide it before we add the pieces.
 *
 * Whenever we add a segment, we need to decide if the join
 * is a smooth connection, a right turn, or a left turn. For
 * sharp turns, we add a line join on the one side, and intersect
 * the offset curves on the other.
 *
 * Since the intersection shortens both segments, we have to
 * delay adding the previous segments to the outlines until
 * we've handled the join at their end. We also need to hold
 * off on adding the initial segment to the outlines until
 * we've seen the end of the current contour of the path, to
 * handle the join at before the initial segment for closed
 * contours.
 *
 * If the contour turns out to not be closed when we reach
 * the end, we collect the pending segments, reverse the
 * left contour, and connect the right and left contour
 * with end caps, closing the resulting outline.
 *
 * If the path isn't done after we've finished handling the
 * outlines of the current contour, we start over with
 * collecting offset segments of the next contour.
 *
 * We rely on the ability to offset, subdivide, intersect
 * and reverse curves.
 */
typedef struct
{
  GskPathBuilder *builder;  // builder that collects the stroke
  GskStroke *stroke;  // stroke parameters

  GskPathBuilder *left;  // accumulates the left contour
  GskPathBuilder *right; // accumulates the right contour

  gboolean has_current_point;  // seg0 has been set from a move
  gboolean has_current_curve;  // seg has been set from a curve
  gboolean is_first_curve; // seg is the first segment we've seen

  Segment l;
  Segment r;
  Segment l0;
  Segment r0;

#ifdef STROKE_DEBUG
  GskPathBuilder *debug;
#endif
} StrokeData;

static void
stroke_data_init (StrokeData     *stroke_data,
                  GskPathBuilder *builder,
                  GskStroke      *stroke)
{
  memset (stroke_data, 0, sizeof (StrokeData));
  stroke_data->builder = builder;
  stroke_data->stroke = stroke;
  segment_init (&stroke_data->l, NULL);
  segment_init (&stroke_data->r, NULL);
  segment_init (&stroke_data->l0, NULL);
  segment_init (&stroke_data->r0, NULL);
}

static void
stroke_data_clear (StrokeData *stroke_data)
{
  g_assert (stroke_data->left == NULL);
  g_assert (stroke_data->right == NULL);
  segment_clear (&stroke_data->l);
  segment_clear (&stroke_data->r);
  segment_clear (&stroke_data->l0);
  segment_clear (&stroke_data->r0);
}

#ifdef STROKE_DEBUG
static void
add_debug_point (StrokeData *stroke_data,
                 const graphene_point_t *p,
                 int               width)
{
  gsk_path_builder_add_circle (stroke_data->debug, &p[0], width);
}

static void
add_debug (StrokeData     *stroke_data,
           const GskCurve *curve,
           int             width)
{
  const graphene_point_t *p;

  switch (curve->op)
    {
    case GSK_PATH_LINE:
    case GSK_PATH_CLOSE:
      p = curve->line.points;
      add_debug_point (stroke_data, &p[0], width);
      add_debug_point (stroke_data, &p[1], width);
      break;
    case GSK_PATH_QUAD:
      p = curve->quad.points;
      path_builder_move_to_point (stroke_data->debug, &p[0]);
      path_builder_line_to_point (stroke_data->debug, &p[1]);
      path_builder_line_to_point (stroke_data->debug, &p[2]);
      add_debug_point (stroke_data, &p[0], width);
      add_debug_point (stroke_data, &p[1], width / 2);
      add_debug_point (stroke_data, &p[2], width);
      break;
    case GSK_PATH_CUBIC:
      p = curve->cubic.points;
      path_builder_move_to_point (stroke_data->debug, &p[0]);
      path_builder_line_to_point (stroke_data->debug, &p[1]);
      path_builder_line_to_point (stroke_data->debug, &p[2]);
      path_builder_line_to_point (stroke_data->debug, &p[3]);
      add_debug_point (stroke_data, &p[0], width);
      add_debug_point (stroke_data, &p[1], width / 2);
      add_debug_point (stroke_data, &p[2], width / 2);
      add_debug_point (stroke_data, &p[3], width);
      break;
    case GSK_PATH_CONIC:
      p = curve->conic.points;
      path_builder_move_to_point (stroke_data->debug, &p[0]);
      path_builder_line_to_point (stroke_data->debug, &p[1]);
      path_builder_line_to_point (stroke_data->debug, &p[3]);
      add_debug_point (stroke_data, &p[0], width);
      add_debug_point (stroke_data, &p[1], width / 2);
      add_debug_point (stroke_data, &p[3], width);
      break;
    case GSK_PATH_MOVE:
      break;
    default:
      g_assert_not_reached ();
    }
}
#endif

/* Append the prev segment to the right contour */
static void
append_right (StrokeData *stroke_data)
{
  if (stroke_data->is_first_curve)
    {
      const GskCurve *r = segment_get_last_curve (&stroke_data->r);
      path_builder_move_to_point (stroke_data->right, gsk_curve_get_end_point (r));
    }
  else
    {
      path_builder_add_curves (stroke_data->right, stroke_data->r.c);
    }
}

/* Append the prev segment to the left contour */
static void
append_left (StrokeData *stroke_data)
{
  if (stroke_data->is_first_curve)
    {
      const GskCurve *l = segment_get_last_curve (&stroke_data->l);
      path_builder_move_to_point (stroke_data->left, gsk_curve_get_end_point (l));
    }
  else
    {
      path_builder_add_curves (stroke_data->left, stroke_data->l.c);
    }
}

/* Add the previous segments, and the join between it and the new segment,
 * and update stroke_data->seg to segment.
 *
 * If stroke_data->seg is the first segment of the contour, we don't
 * add it yet, but save it in stroke_data->seg0 for later when we know if
 * the contour is closed or not.
 */
static void
add_segment (StrokeData *stroke_data,
             Segment    *left,
             Segment    *right)
{
  float angle;
  graphene_vec2_t tangent1, tangent2;
  GskLineJoin line_join;
  const GskCurve *c1, *c2;
  const GskCurve *r1, *r2;
  const GskCurve *l1, *l2;

#ifdef STROKE_DEBUG
  {
    const char *s;
    if ((s = g_getenv ("STROKE_DEBUG")) != NULL)
      {
        if (strstr (s, "o"))
          add_debug (stroke_data, &left->curve, 3);
        if (strstr (s, "l"))
          for (unsigned int i = 0; i < segment_get_n_curves (left); i++)
            add_debug (stroke_data, segment_get_curve (left, i), 2);
        if (strstr (s, "r"))
          for (unsigned int i = 0; i < segment_get_n_curves (right); i++)
            add_debug (stroke_data, segment_get_curve (right, i), 2);
      }
  }
#endif

  if (!stroke_data->has_current_curve)
    {
      r2 = segment_get_first_curve (right);
      l2 = segment_get_first_curve (left);

      path_builder_move_to_point (stroke_data->right, gsk_curve_get_start_point (r2));
      path_builder_move_to_point (stroke_data->left, gsk_curve_get_start_point (l2));

      segment_copy (left, &stroke_data->l0);
      segment_copy (right, &stroke_data->r0);
      segment_copy (left, &stroke_data->l);
      segment_copy (right, &stroke_data->r);

      stroke_data->has_current_curve = TRUE;
      stroke_data->is_first_curve = TRUE;
      return;
    }

  c1 = &stroke_data->l.curve;
  r1 = segment_get_last_curve (&stroke_data->r);
  l1 = segment_get_last_curve (&stroke_data->l);
  c2 = &left->curve;
  r2 = segment_get_first_curve (right);
  l2 = segment_get_first_curve (left);

  /* Look at the angle between the segments */
  gsk_curve_get_end_tangent (c1, &tangent1);
  gsk_curve_get_start_tangent (c2, &tangent2);

  angle = angle_between (&tangent1, &tangent2);

  if (fabs (angle) < DEG_TO_RAD (5))
    line_join = GSK_LINE_JOIN_ROUND;
  else
    line_join = stroke_data->stroke->line_join;

  if (fabs (angle) < DEG_TO_RAD (1))
    {
      /* Close enough to a straight line */
      append_right (stroke_data);
      append_left (stroke_data);
    }
  else
    {
      float maxdist;

      if (fabs (M_PI - fabs (angle)) < DEG_TO_RAD (1))
        {
          /* For 180 turns, we look at the whole curves to
           * decide if they are left or right turns
           */
          get_tangent (gsk_curve_get_start_point (c1),
                       gsk_curve_get_end_point (c1),
                       &tangent1);
          get_tangent (gsk_curve_get_start_point (c2),
                       gsk_curve_get_end_point (c2),
                       &tangent2);
          angle = angle_between (&tangent1, &tangent2);
        }

      maxdist = find_closest_intersection (c1, c2, NULL);

      if (angle > 0)
        {
          int i, j;
          float t1, t2;
          graphene_point_t p;

          /* Right turn */
          if (segments_intersect (stroke_data->r.c, right->c, FALSE, &i, &j, &t1, &t2, &p, 1) > 0 &&
              graphene_point_distance (&p, gsk_curve_get_start_point (c2), NULL, NULL) < maxdist)
            {
              GskCurve c;
              GskCurve *r;

              g_array_set_size (stroke_data->r.c, i + 1);
              r = &g_array_index (stroke_data->r.c, GskCurve, i);
              gsk_curve_split (r, t1, &c, NULL);
              g_array_index (stroke_data->r.c, GskCurve, i) = c;

              g_array_remove_range (right->c, 0, j);
              r = &g_array_index (right->c, GskCurve, 0);
              gsk_curve_split (r, t2, NULL, &c);
              g_array_index (right->c, GskCurve, 0) = c;

              if (stroke_data->is_first_curve)
                {
                  g_array_set_size (stroke_data->r0.c, 0);
                  g_array_append_vals (stroke_data->r0.c, stroke_data->r.c->data, stroke_data->r.c->len);
                }

              append_right (stroke_data);
            }
          else
            {
              append_right (stroke_data);
              path_builder_line_to_point (stroke_data->right, gsk_curve_get_start_point (r2));
            }

          append_left (stroke_data);

          add_line_join (stroke_data->left,
                         line_join,
                         stroke_data->stroke->line_width / 2,
                         stroke_data->stroke->miter_limit,
                         gsk_curve_get_start_point (c2),
                         l1,
                         l2,
                         angle);
        }
      else
        {
          int i, j;
          float t1, t2;
          graphene_point_t p;

          /* Left turn */
          append_right (stroke_data);

          add_line_join (stroke_data->right,
                         line_join,
                         stroke_data->stroke->line_width / 2,
                         stroke_data->stroke->miter_limit,
                         gsk_curve_get_start_point (c2),
                         r1,
                         r2,
                         angle);

          if (segments_intersect (stroke_data->l.c, left->c, FALSE, &i, &j, &t1, &t2, &p, 1) > 0 &&
              graphene_point_distance (&p, gsk_curve_get_start_point (c2), NULL, NULL) < maxdist)
            {
              GskCurve c;
              GskCurve *l;

              g_array_set_size (stroke_data->l.c, i + 1);
              l = &g_array_index (stroke_data->l.c, GskCurve, i);
              gsk_curve_split (l, t1, &c, NULL);
              g_array_index (stroke_data->l.c, GskCurve, i) = c;

              g_array_remove_range (left->c, 0, j);
              l = &g_array_index (left->c, GskCurve, 0);
              gsk_curve_split (l, t2, NULL, &c);
              g_array_index (left->c, GskCurve, 0) = c;

              if (stroke_data->is_first_curve)
                {
                  g_array_set_size (stroke_data->l0.c, 0);
                  g_array_append_vals (stroke_data->l0.c, stroke_data->l.c->data, stroke_data->l.c->len);
                }

              append_left (stroke_data);
            }
          else
            {
              append_left (stroke_data);
              path_builder_line_to_point (stroke_data->left, gsk_curve_get_start_point (l2));
            }
        }
    }

  segment_copy (left, &stroke_data->l);
  segment_copy (right, &stroke_data->r);

  stroke_data->is_first_curve = FALSE;
}

/* Create a single closed contour and add it to
 * stroke_data->builder, by connecting the right and the
 * reversed left contour with caps.
 *
 * After this call, stroke_data->left and ->right are NULL.
 */
static void
cap_and_connect_contours (StrokeData *stroke_data)
{
  GskPath *path;
  const graphene_point_t *r0, *l0, *r1, *l1;

  r1 = r0 = gsk_curve_get_start_point (segment_get_first_curve (&stroke_data->r0));
  l1 = l0 = gsk_curve_get_start_point (segment_get_first_curve (&stroke_data->l0));

  if (stroke_data->has_current_curve)
    {
      path_builder_add_curves (stroke_data->right, stroke_data->r.c);
      path_builder_add_curves (stroke_data->left, stroke_data->l.c);

      r1 = gsk_curve_get_end_point (segment_get_last_curve (&stroke_data->r));
      l1 = gsk_curve_get_end_point (segment_get_last_curve (&stroke_data->l));
    }
  else
    path_builder_move_to_point (stroke_data->right, r1);

  add_line_cap (stroke_data->right,
                stroke_data->stroke->line_cap,
                stroke_data->stroke->line_width,
                r1, l1);

  if (stroke_data->has_current_curve)
    {
      path = gsk_path_builder_free_to_path (stroke_data->left);
      path_builder_add_reverse_path (stroke_data->right, path);
      gsk_path_unref (path);

      if (!stroke_data->is_first_curve)
        {
          /* Add the first segment that wasn't added initially */
          unsigned int n = segment_get_n_curves (&stroke_data->l0);
          for (unsigned int i = 0; i < n; i++)
            {
              GskCurve r;
              gsk_curve_reverse (segment_get_curve (&stroke_data->l0, n - 1 - i), &r);
              path_builder_add_curve (stroke_data->right, &r);
            }
        }
    }
  else
    gsk_path_builder_unref (stroke_data->left);

  add_line_cap (stroke_data->right,
                stroke_data->stroke->line_cap,
                stroke_data->stroke->line_width,
                l0, r0);

  if (stroke_data->has_current_curve)
    {
      if (!stroke_data->is_first_curve)
        {
          /* Add the first segment that wasn't added initially */
          unsigned int n = segment_get_n_curves (&stroke_data->r0);
          for (unsigned int i = 0; i < n; i++)
            {
              path_builder_add_curve (stroke_data->right, segment_get_curve (&stroke_data->r0, i));
            }
        }
    }

  gsk_path_builder_close (stroke_data->right);

  path = gsk_path_builder_free_to_path (stroke_data->right);
  gsk_path_builder_add_path (stroke_data->builder, path);
  gsk_path_unref (path);

  stroke_data->left = NULL;
  stroke_data->right = NULL;
}

/* Close the left and the right contours and add them to
 * stroke_data->builder.
 *
 * After this call, stroke_data->left and ->right are NULL.
 */
static void
close_contours (StrokeData *stroke_data)
{
  GskPath *path;
  GskPathBuilder *builder;

  if (stroke_data->has_current_curve)
    {
      /* add final join and first segment */
      add_segment (stroke_data, &stroke_data->l0, &stroke_data->r0);
      path_builder_add_curves (stroke_data->right, stroke_data->r.c);
      path_builder_add_curves (stroke_data->left, stroke_data->l.c);
    }

  gsk_path_builder_close (stroke_data->right);

  path = gsk_path_builder_free_to_path (stroke_data->right);
  gsk_path_builder_add_path (stroke_data->builder, path);
  gsk_path_unref (path);

  builder = gsk_path_builder_new ();
  path_builder_move_to_point (builder, gsk_path_builder_get_current_point (stroke_data->left));
  path = gsk_path_builder_free_to_path (stroke_data->left);
  path_builder_add_reverse_path (builder, path);
  gsk_path_unref (path);
  gsk_path_builder_close (builder);

  path = gsk_path_builder_free_to_path (builder);
  gsk_path_builder_add_path (stroke_data->builder, path);
  gsk_path_unref (path);

  stroke_data->left = NULL;
  stroke_data->right = NULL;
}

static void
add_segment_for_curve (const GskCurve *curve,
                       StrokeData     *stroke_data)
{
  Segment left = { 0, };
  Segment right = { 0, };

  segment_init (&left, curve);
  segment_subdivide (&left);
  segment_copy (&left, &right);

  segment_offset (&left, - stroke_data->stroke->line_width / 2);
  segment_offset (&right, stroke_data->stroke->line_width / 2);

  elide_self_intersections (&left);
  elide_self_intersections (&right);

  add_segment (stroke_data, &left, &right);

  segment_clear (&left);
  segment_clear (&right);
}

static gboolean
stroke_op (GskPathOperation        op,
           const graphene_point_t *pts,
           gsize                   n_pts,
           float                   weight,
           gpointer                user_data)
{
  StrokeData *stroke_data = user_data;
  GskCurve curve;
  Segment left = { 0, };
  Segment right = { 0, };

  switch (op)
    {
    case GSK_PATH_MOVE:
      if (stroke_data->has_current_point)
        cap_and_connect_contours (stroke_data);

      gsk_curve_init_foreach (&curve,
                              GSK_PATH_LINE,
                              (const graphene_point_t[]) { pts[0], GRAPHENE_POINT_INIT (pts[0].x + 1, pts[0].y) },
                              2, 0.f);

      segment_init (&left, &curve);
      segment_subdivide (&left);
      segment_copy (&left, &right);

      segment_offset (&left, - stroke_data->stroke->line_width / 2);
      segment_offset (&right, stroke_data->stroke->line_width / 2);

      segment_copy (&left, &stroke_data->l0);
      segment_copy (&right, &stroke_data->r0);

      segment_clear (&left);
      segment_clear (&right);

      stroke_data->right = gsk_path_builder_new ();
      stroke_data->left = gsk_path_builder_new ();

      stroke_data->has_current_point = TRUE;
      stroke_data->has_current_curve = FALSE;
      break;

    case GSK_PATH_CLOSE:
      if (stroke_data->has_current_point)
        {
          if (!graphene_point_near (&pts[0], &pts[1], 0.001))
            {
              gsk_curve_init_foreach (&curve, GSK_PATH_LINE, pts, n_pts, weight);
              add_segment_for_curve (&curve, stroke_data);
            }
          close_contours (stroke_data);
        }

      stroke_data->has_current_point = FALSE;
      stroke_data->has_current_curve = FALSE;
      break;

    case GSK_PATH_LINE:
    case GSK_PATH_QUAD:
    case GSK_PATH_CONIC:
      gsk_curve_init_foreach (&curve, op, pts, n_pts, weight);
      add_segment_for_curve (&curve, stroke_data);
      break;

    case GSK_PATH_CUBIC:
      gsk_curve_init_foreach (&curve, op, pts, n_pts, weight);
      split_curve_by_cusps (&curve, (AddCurveCallback)add_segment_for_curve, stroke_data);
      break;

    default:
      g_assert_not_reached ();
    }

  return TRUE;
}

/*
 * gsk_contour_default_add_stroke:
 * @contour: the GskContour to stroke
 * @builder: a GskPathBuilder to add the results to
 * @stroke: stroke parameters
 *
 * Strokes @contour according to the parameters given in @stroke,
 * and adds the resulting curves to @builder. Note that stroking
 * a contour will in general produce multiple contours - either
 * because @contour is closed and has a left and right outline,
 * or because @stroke requires dashes.
 */
void
gsk_contour_default_add_stroke (const GskContour *contour,
                                GskPathBuilder   *builder,
                                GskStroke        *stroke)
{
  StrokeData stroke_data;

  stroke_data_init (&stroke_data, builder, stroke);

#ifdef STROKE_DEBUG
  stroke_data.debug = gsk_path_builder_new ();
#endif

  if (stroke->dash_length <= 0)
    gsk_contour_foreach (contour, GSK_PATH_TOLERANCE_DEFAULT, stroke_op, &stroke_data);
  else
    gsk_contour_dash (contour, stroke, GSK_PATH_TOLERANCE_DEFAULT, stroke_op, &stroke_data);

  if (stroke_data.has_current_point)
    cap_and_connect_contours (&stroke_data);

#ifdef STROKE_DEBUG
  GskPath *path = gsk_path_builder_free_to_path (stroke_data.debug);
  gsk_path_builder_add_path (builder, path);
  gsk_path_unref (path);
#endif

  stroke_data_clear (&stroke_data);
}

/* }}} */

/* vim:set foldmethod=marker expandtab: */