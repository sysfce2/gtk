#include <gtk/gtk.h>
#include "gsk/gskcurveprivate.h"

static void
init_random_point (graphene_point_t *p)
{
  p->x = g_test_rand_double_range (0, 1000);
  p->y = g_test_rand_double_range (0, 1000);
}

static float
random_weight (void)
{
  if (g_test_rand_bit ())
    return g_test_rand_double_range (1, 20);
  else
    return 1.0 / g_test_rand_double_range (1, 20);
}

static void
init_random_curve_with_op (GskCurve         *curve,
                           GskPathOperation  min_op,
                           GskPathOperation  max_op)
{
  switch (g_test_rand_int_range (min_op, max_op + 1))
    {
    case GSK_PATH_LINE:
      {
        graphene_point_t p[2];

        init_random_point (&p[0]);
        init_random_point (&p[1]);
        gsk_curve_init (curve, gsk_pathop_encode (GSK_PATH_LINE, p));
      }
      break;

    case GSK_PATH_QUAD:
      {
        graphene_point_t p[3];

        init_random_point (&p[0]);
        init_random_point (&p[1]);
        init_random_point (&p[2]);
        gsk_curve_init (curve, gsk_pathop_encode (GSK_PATH_QUAD, p));
      }
    break;

    case GSK_PATH_CUBIC:
      {
        graphene_point_t p[4];

        init_random_point (&p[0]);
        init_random_point (&p[1]);
        init_random_point (&p[2]);
        init_random_point (&p[3]);
        gsk_curve_init (curve, gsk_pathop_encode (GSK_PATH_CUBIC, p));
      }
    break;

    case GSK_PATH_CONIC:
      {
        graphene_point_t p[4];

        init_random_point (&p[0]);
        init_random_point (&p[1]);
        p[2] = GRAPHENE_POINT_INIT (random_weight(), 0);
        init_random_point (&p[3]);
        gsk_curve_init (curve, gsk_pathop_encode (GSK_PATH_CONIC, p));
      }
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
init_random_curve (GskCurve *curve)
{
  init_random_curve_with_op (curve, GSK_PATH_LINE, GSK_PATH_CONIC);
}

static void
test_curve_tangents (void)
{
  for (int i = 0; i < 100; i++)
    {
      GskCurve c;
      graphene_vec2_t vec, exact;

      init_random_curve (&c);

      gsk_curve_get_tangent (&c, 0, &vec);
      g_assert_cmpfloat_with_epsilon (graphene_vec2_length (&vec), 1.0f, 0.00001);
      gsk_curve_get_start_tangent (&c, &exact);
      g_assert_cmpfloat_with_epsilon (graphene_vec2_length (&exact), 1.0f, 0.00001);
      g_assert_true (graphene_vec2_near (&vec, &exact, 0.05));

      gsk_curve_get_tangent (&c, 1, &vec);
      g_assert_cmpfloat_with_epsilon (graphene_vec2_length (&vec), 1.0f, 0.00001);
      gsk_curve_get_end_tangent (&c, &exact);
      g_assert_cmpfloat_with_epsilon (graphene_vec2_length (&exact), 1.0f, 0.00001);
      g_assert_true (graphene_vec2_near (&vec, &exact, 0.05));
    }
}

static void
test_curve_points (void)
{
  for (int i = 0; i < 100; i++)
    {
      GskCurve c;
      graphene_point_t p;

      init_random_curve (&c);

      /* We can assert equality here because evaluating the polynomials with 0
       * has no effect on accuracy.
       */
      gsk_curve_get_point (&c, 0, &p);
      g_assert_true (graphene_point_equal (gsk_curve_get_start_point (&c), &p));
      /* But here we evaluate the polynomials with 1 which gives the highest possible
       * accuracy error. So we'll just be generous here.
       */
      gsk_curve_get_point (&c, 1, &p);
      g_assert_true (graphene_point_near (gsk_curve_get_end_point (&c), &p, 0.05));
    }
}

/* at this point the subdivision stops and the decomposer
 * violates tolerance rules
 */
#define MIN_PROGRESS (1/1024.f)

typedef struct
{
  graphene_point_t p;
  float t;
} PointOnLine;

static gboolean
add_line_to_array (const graphene_point_t *from,
                   const graphene_point_t *to,
                   float                   from_progress,
                   float                   to_progress,
                   GskCurveLineReason      reason,
                   gpointer                user_data)
{
  GArray *array = user_data;
  PointOnLine *last = &g_array_index (array, PointOnLine, array->len - 1);

  g_assert_true (array->len > 0);
  g_assert_cmpfloat (from_progress, >=, 0.0f);
  g_assert_cmpfloat (from_progress, <, to_progress);
  g_assert_cmpfloat (to_progress, <=, 1.0f);

  g_assert_true (graphene_point_equal (&last->p, from));
  g_assert_cmpfloat (last->t, ==, from_progress);

  g_array_append_vals (array, (PointOnLine[1]) { { *to, to_progress } }, 1);

  return TRUE;
}

static void
test_curve_decompose (void)
{
  static const float tolerance = 0.5;

  for (int i = 0; i < 100; i++)
    {
      GArray *array;
      GskCurve c;

      init_random_curve (&c);

      array = g_array_new (FALSE, FALSE, sizeof (PointOnLine));
      g_array_append_vals (array, (PointOnLine[1]) { { *gsk_curve_get_start_point (&c), 0.f } }, 1);

      g_assert_true (gsk_curve_decompose (&c, tolerance, add_line_to_array, array));

      g_assert_cmpint (array->len, >=, 2); /* We at least got a line to the end */
      g_assert_cmpfloat (g_array_index (array, PointOnLine, array->len - 1).t, ==, 1.0);

      for (int j = 0; j < array->len; j++)
        {
          PointOnLine *pol = &g_array_index (array, PointOnLine, j);
          graphene_point_t p;

          /* Check that the points we got are actually on the line */
          gsk_curve_get_point (&c, pol->t, &p);
          g_assert_true (graphene_point_near (&pol->p, &p, 0.05));

          /* Check that the mid point is not further than the tolerance */
          if (j > 0)
            {
              PointOnLine *last = &g_array_index (array, PointOnLine, j - 1);
              graphene_point_t mid;

              if (pol->t - last->t > MIN_PROGRESS)
                {
                  graphene_point_interpolate (&last->p, &pol->p, 0.5, &mid);
                  gsk_curve_get_point (&c, (pol->t + last->t) / 2, &p);
                  /* The decomposer does this cheaper Manhattan distance test,
                   * so graphene_point_near() does not work */
                  g_assert_cmpfloat (fabs (mid.x - p.x), <=, tolerance);
                  g_assert_cmpfloat (fabs (mid.y - p.y), <=, tolerance);
                }
            }
        }

      g_array_unref (array);
    }
}

/* Test that reversing curves of all types produces the
 * expected result
 */
static void
test_curve_reverse (void)
{
  GskCurve c, r;
  graphene_point_t p[4];

  graphene_point_init (&p[0], 0, 0);
  graphene_point_init (&p[1], 50, 50);
  gsk_curve_init (&c, gsk_pathop_encode (GSK_PATH_LINE, p));

  gsk_curve_reverse (&c, &r);

  g_assert_true (r.op == GSK_PATH_LINE);
  g_assert_true (graphene_point_equal (&r.line.points[0], &p[1]));
  g_assert_true (graphene_point_equal (&r.line.points[1], &p[0]));

  graphene_point_init (&p[0], 0, 0);
  graphene_point_init (&p[1], 50, 50);
  graphene_point_init (&p[2], 100, 50);
  graphene_point_init (&p[3], 200, 0);
  gsk_curve_init (&c, gsk_pathop_encode (GSK_PATH_CUBIC, p));

  gsk_curve_reverse (&c, &r);
  g_assert_true (r.op == GSK_PATH_CUBIC);
  g_assert_true (graphene_point_equal (&r.cubic.points[0], &p[3]));
  g_assert_true (graphene_point_equal (&r.cubic.points[1], &p[2]));
  g_assert_true (graphene_point_equal (&r.cubic.points[2], &p[1]));
  g_assert_true (graphene_point_equal (&r.cubic.points[3], &p[0]));

  graphene_point_init (&p[0], 0, 0);
  graphene_point_init (&p[1], 50, 50);
  graphene_point_init (&p[2], 100, 50);
  gsk_curve_init_foreach (&c, GSK_PATH_CONIC, p, 3, 20);

  gsk_curve_reverse (&c, &r);
  g_assert_true (r.op == GSK_PATH_CONIC);
  g_assert_true (r.conic.points[2].x == 20);

  g_assert_true (graphene_point_equal (&r.conic.points[0], &c.conic.points[3]));
  g_assert_true (graphene_point_equal (&r.conic.points[1], &c.conic.points[1]));
  g_assert_true (graphene_point_equal (&r.conic.points[3], &c.conic.points[0]));
}

static gboolean
add_curve_to_array (GskPathOperation        op,
                    const graphene_point_t *pts,
                    gsize                   n_pts,
                    float                   weight,
                    gpointer                user_data)
{
  GArray *array = user_data;
  GskCurve c;

  gsk_curve_init_foreach (&c, op, pts, n_pts, weight);
  g_array_append_val (array, c);

  return TRUE;
}

static void
test_curve_decompose_conic (void)
{
  g_test_skip ("No good error bounds for decomposing conics");
  return;

  for (int i = 0; i < 100; i++)
    {
      GArray *array;
      GskCurve c;
      GskPathBuilder *builder;
      GskPath *path;
      GskPathMeasure *measure;
      const graphene_point_t *s;

      init_random_curve_with_op (&c, GSK_PATH_CONIC, GSK_PATH_CONIC);

      builder = gsk_path_builder_new ();

      s = gsk_curve_get_start_point (&c);
      gsk_path_builder_move_to (builder, s->x, s->y);
      gsk_curve_builder_to (&c, builder);
      path = gsk_path_builder_free_to_path (builder);
      measure = gsk_path_measure_new_with_tolerance (path, 0.1);

      array = g_array_new (FALSE, FALSE, sizeof (GskCurve));

      g_assert_true (gsk_curve_decompose_curve (&c, GSK_PATH_FOREACH_ALLOW_CUBIC, 0.1, add_curve_to_array, array));

      g_assert_cmpint (array->len, >=, 1);

      for (int j = 0; j < array->len; j++)
        {
          GskCurve *c2 = &g_array_index (array, GskCurve, j);

          g_assert_true (c2->op == GSK_PATH_CUBIC);

          /* Check that the curves we got are approximating the conic */
          for (int k = 0; k < 11; k++)
            {
              graphene_point_t p;
              float dist;

              gsk_curve_get_point (c2, k/10.0, &p);
              dist = gsk_path_measure_get_closest_point (measure, &p, NULL);
              g_assert_cmpfloat (dist, <, 0.5); // FIXME error bound ?
            }
        }

      g_array_unref (array);

      gsk_path_measure_unref (measure);
      gsk_path_unref (path);
    }
}

static void
test_curve_decompose_into (GskPathForeachFlags flags)
{
  for (int i = 0; i < 100; i++)
    {
      GskCurve c;
      GskPathBuilder *builder;
      const graphene_point_t *s;
      GskPath *path;
      GArray *array;

      init_random_curve (&c);

      builder = gsk_path_builder_new ();

      s = gsk_curve_get_start_point (&c);
      gsk_path_builder_move_to (builder, s->x, s->y);
      gsk_curve_builder_to (&c, builder);
      path = gsk_path_builder_free_to_path (builder);

      array = g_array_new (FALSE, FALSE, sizeof (GskCurve));

      g_assert_true (gsk_curve_decompose_curve (&c, flags, 0.1, add_curve_to_array, array));

      g_assert_cmpint (array->len, >=, 1);

      for (int j = 0; j < array->len; j++)
        {
          GskCurve *c2 = &g_array_index (array, GskCurve, j);

          switch (c2->op)
            {
            case GSK_PATH_MOVE:
            case GSK_PATH_CLOSE:
            case GSK_PATH_LINE:
              break;
            case GSK_PATH_QUAD:
              g_assert_true (flags & GSK_PATH_FOREACH_ALLOW_QUAD);
              break;
            case GSK_PATH_CUBIC:
              g_assert_true (flags & GSK_PATH_FOREACH_ALLOW_CUBIC);
              break;
            case GSK_PATH_CONIC:
              g_assert_true (flags & GSK_PATH_FOREACH_ALLOW_CONIC);
              break;
            default:
              g_assert_not_reached ();
            }
        }

      g_array_unref (array);
      gsk_path_unref (path);
    }
}

static void
test_curve_decompose_into_line (void)
{
  test_curve_decompose_into (0);
}

static void
test_curve_decompose_into_quad (void)
{
  test_curve_decompose_into (GSK_PATH_FOREACH_ALLOW_QUAD);
}

static void
test_curve_decompose_into_cubic (void)
{
  test_curve_decompose_into (GSK_PATH_FOREACH_ALLOW_CUBIC);
}

int
main (int argc, char *argv[])
{
  (g_test_init) (&argc, &argv, NULL);

  g_test_add_func ("/curve/points", test_curve_points);
  g_test_add_func ("/curve/tangents", test_curve_tangents);
  g_test_add_func ("/curve/decompose", test_curve_decompose);
  g_test_add_func ("/curve/reverse", test_curve_reverse);
  g_test_add_func ("/curve/decompose/conic", test_curve_decompose_conic);
  g_test_add_func ("/curve/decompose/into/line", test_curve_decompose_into_line);
  g_test_add_func ("/curve/decompose/into/quad", test_curve_decompose_into_quad);
  g_test_add_func ("/curve/decompose/into/cubic", test_curve_decompose_into_cubic);

  return g_test_run ();
}