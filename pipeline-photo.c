/*
 * Copyright (C) 2007 daniel g. siegel <dgsiegel@gmail.com>
 * All rights reserved. This software is released under the GPL2 licence.
 */

#include <gst/interfaces/xoverlay.h>
#include <glib.h>

#include "cheese.h"
#include "pipeline-photo.h"
#include "window.h"

G_DEFINE_TYPE (Pipeline, pipeline, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;

typedef struct _PipelinePrivate PipelinePrivate;
typedef struct _gsteffects gsteffects;

struct _gsteffects
{
  gchar *name;
  gchar *effect;
};

struct _PipelinePrivate
{
  int picture_requested;

  GstElement *source;
  GstElement *ffmpeg1, *ffmpeg2, *ffmpeg3;
  GstElement *tee;
  GstElement *queuevid, *queueimg;
  GstElement *caps;
  GstElement *effect;

  GArray *effects;
  int used_effect;
};

#define PIPELINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PIPELINE_TYPE, PipelinePrivate))

// private methods
static gboolean cb_have_data(GstElement *element, GstBuffer *buffer, GstPad *pad, gpointer user_data);
static void pipeline_lens_open(Pipeline *self);
static void pipeline_set_effect(Pipeline *self, gchar *e);

void
pipeline_set_play(Pipeline *self)
{
  gst_element_set_state(PIPELINE(self)->pipeline, GST_STATE_PLAYING);
  return;
}

void
pipeline_set_stop(Pipeline *self)
{
  gst_element_set_state(PIPELINE(self)->pipeline, GST_STATE_NULL);
  return;
}

void
pipeline_button_clicked(GtkWidget *widget, gpointer self)
{
  pipeline_lens_open(self);
  return;
}

static void
pipeline_lens_open(Pipeline *self)
{
  PipelinePrivate *priv = PIPELINE_GET_PRIVATE(self);
  priv->picture_requested = TRUE;
  return;
}

GstElement *
pipeline_get_ximagesink(Pipeline *self)
{
  return PIPELINE(self)->ximagesink;
}

GstElement *
pipeline_get_fakesink(Pipeline *self)
{
  return PIPELINE(self)->fakesink;
}

GstElement *
pipeline_get_pipeline(Pipeline *self)
{
  return PIPELINE(self)->pipeline;
}

static void
pipeline_set_effect(Pipeline *self, gchar *e)
{
  PipelinePrivate *priv = PIPELINE_GET_PRIVATE(self);

  pipeline_set_stop(self);

  gst_element_unlink(priv->ffmpeg1, priv->effect);
  gst_element_unlink(priv->effect, priv->ffmpeg2);
  gst_bin_remove(GST_BIN(PIPELINE(self)->pipeline), priv->effect);

  printf("changing to effect: %s\n", e);
  //priv->effect = gst_element_factory_make(e, "effect");
  //priv->effect = gst_parse_bin_from_description("videoflip method=4", TRUE, NULL);
  priv->effect = gst_parse_bin_from_description(e, TRUE, NULL);
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->effect);

  gst_element_link(priv->ffmpeg1, priv->effect);
  gst_element_link(priv->effect, priv->ffmpeg2);

  pipeline_set_play(self);
  set_screen_x_window_id();
}

void
pipeline_change_effect_next(GtkWidget *widget, gpointer self)
{
  PipelinePrivate *priv = PIPELINE_GET_PRIVATE(self);

  if (g_array_index(priv->effects, gsteffects, priv->used_effect + 1).name == NULL)
    priv->used_effect = 0;
  else
    priv->used_effect++;

  pipeline_set_effect(self, g_array_index(priv->effects, gsteffects, priv->used_effect).effect);
  set_effects_label(g_array_index(priv->effects, gsteffects, priv->used_effect).name);
}

void
pipeline_change_effect_prev(GtkWidget *widget, gpointer self)
{
  PipelinePrivate *priv = PIPELINE_GET_PRIVATE(self);

  if (priv->used_effect == 0) {
    int i = 0;
    while (g_array_index(priv->effects, gsteffects, i + 1).name != NULL)
      i++;
    priv->used_effect = i;
  }
  else
    priv->used_effect--;

  pipeline_set_effect(self, g_array_index(priv->effects, gsteffects, priv->used_effect).effect);
  set_effects_label(g_array_index(priv->effects, gsteffects, priv->used_effect).name);
}

void
pipeline_create(Pipeline *self) {

  GstCaps *filter;
  gboolean link_ok; 

  PipelinePrivate *priv = PIPELINE_GET_PRIVATE(self);

  PIPELINE(self)->pipeline = gst_pipeline_new("pipeline");
  priv->source = gst_element_factory_make("gconfvideosrc", "v4l2src");
  // if you want to test without having a webcam
  //source = gst_element_factory_make("videotestsrc", "v4l2src");

  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->source);
  priv->ffmpeg1 = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->ffmpeg1);

  priv->used_effect = 0;
  priv->effect = gst_element_factory_make(g_array_index(priv->effects, gsteffects, priv->used_effect).effect, "effect");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->effect);

  priv->ffmpeg2 = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace2");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->ffmpeg2);
  priv->ffmpeg3 = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace3");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->ffmpeg3);
  priv->queuevid = gst_element_factory_make("queue", "queuevid");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->queuevid);
  priv->queueimg = gst_element_factory_make("queue", "queueimg");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->queueimg);
  priv->caps = gst_element_factory_make("capsfilter", "capsfilter");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->caps);
  PIPELINE(self)->ximagesink = gst_element_factory_make("gconfvideosink", "gconfvideosink");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), PIPELINE(self)->ximagesink);
  priv->tee = gst_element_factory_make("tee", "tee");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), priv->tee);
  PIPELINE(self)->fakesink = gst_element_factory_make("fakesink", "fakesink");
  gst_bin_add(GST_BIN(PIPELINE(self)->pipeline), PIPELINE(self)->fakesink);

  /*
   * the pipeline looks like this:
   * gconfvideosrc -> ffmpegcsp -> ffmpegcsp -> tee (filtered) -> queue-> ffmpegcsp -> gconfvideosink
   *                                             |
   *                                         queueimg -> fakesink -> pixbuf (gets picture from data)
   */

  gst_element_link(priv->source, priv->ffmpeg1);
  gst_element_link(priv->ffmpeg1, priv->effect);
  gst_element_link(priv->effect, priv->ffmpeg2);

  filter = gst_caps_new_simple("video/x-raw-rgb",
      "width", G_TYPE_INT, PHOTO_WIDTH,
      "height", G_TYPE_INT, PHOTO_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "bpp", G_TYPE_INT, 24,
      "depth", G_TYPE_INT, 24, NULL);

  link_ok = gst_element_link_filtered(priv->ffmpeg2, priv->tee, filter);
  if (!link_ok) {
    g_warning("Failed to link elements!");
  }
  //FIXME: do we need this?
  //filter = gst_caps_new_simple("video/x-raw-yuv", NULL);

  gst_element_link(priv->tee, priv->queuevid);
  gst_element_link(priv->queuevid, priv->ffmpeg3);

  gst_element_link(priv->ffmpeg3, PIPELINE(self)->ximagesink);

  gst_element_link(priv->tee, priv->queueimg);
  gst_element_link(priv->queueimg, PIPELINE(self)->fakesink);
  g_object_set(G_OBJECT(PIPELINE(self)->fakesink), "signal-handoffs", TRUE, NULL);

  g_signal_connect(G_OBJECT(PIPELINE(self)->fakesink), "handoff", 
      G_CALLBACK(cb_have_data), self);

}

static gboolean
cb_have_data(GstElement *element, GstBuffer *buffer, GstPad *pad, gpointer self)
{
  PipelinePrivate *priv = PIPELINE_GET_PRIVATE(self);

  unsigned char *data_photo = (unsigned char *)GST_BUFFER_DATA(buffer);
  if (priv->picture_requested) {
    priv->picture_requested = FALSE;
    create_photo(data_photo);
  }
  return TRUE;
}

void
pipeline_finalize(GObject *object)
{
  PipelinePrivate *priv = PIPELINE_GET_PRIVATE(object);
  g_array_free(priv->effects, TRUE); 

	(*parent_class->finalize) (object);

	return;
}

Pipeline *
pipeline_new(void)
{
	Pipeline *self = g_object_new(PIPELINE_TYPE, NULL);

	return self;
}

void
pipeline_class_init(PipelineClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent(klass);
	object_class = (GObjectClass*) klass;

	object_class->finalize = pipeline_finalize;
  g_type_class_add_private(klass, sizeof(PipelinePrivate));

	G_OBJECT_CLASS(klass)->finalize = (GObjectFinalizeFunc) pipeline_finalize;

}

void
pipeline_init(Pipeline *self)
{
  PipelinePrivate *priv = PIPELINE_GET_PRIVATE(self);

  priv->effects = g_array_new (TRUE, FALSE, sizeof(gsteffects));

  gsteffects g;
  g.name = "No effect";
  g.effect = "identity";
  g_array_append_val(priv->effects, g);
  g.name = "Edge";
  g.effect = "edgetv";
  g_array_append_val(priv->effects, g);
  g.name = "Aging";
  g.effect = "agingtv";
  g_array_append_val(priv->effects, g);
  g.name = "Dice";
  g.effect = "dicetv";
  g_array_append_val(priv->effects, g);
  g.name = "Warp";
  g.effect = "warptv";
  g_array_append_val(priv->effects, g);
  g.name = "Shagadelic";
  g.effect = "shagadelictv";
  g_array_append_val(priv->effects, g);
  g.name = "Vertigo";
  g.effect = "vertigotv";
  g_array_append_val(priv->effects, g);
  g.name = "Rev";
  g.effect = "revtv";
  g_array_append_val(priv->effects, g);
  g.name = "Quark";
  g.effect = "quarktv";
  g_array_append_val(priv->effects, g);
  g.name = "Horizontal Flip";
  g.effect = "videoflip method=4";
  g_array_append_val(priv->effects, g);
  g.name = "Vertical Flip";
  g.effect = "videoflip method=5";
  g_array_append_val(priv->effects, g);
  g.name = "Hulk";
  g.effect = "videobalance saturation=1.5 hue=-0.5";
  g_array_append_val(priv->effects, g);
  g.name = "Mauve";
  g.effect = "videobalance saturation=1.5 hue=+0.5";
  g_array_append_val(priv->effects, g);
  g.name = "Triste";
  g.effect = "videobalance saturation=0.5 contrast=2";
  g_array_append_val(priv->effects, g);
  g.name = "Saturation";
  g.effect = "videobalance saturation=2 contrast=2";
  g_array_append_val(priv->effects, g);
  g.name = "Noir/Blanc";
  g.effect = "videobalance saturation=0";
  g_array_append_val(priv->effects, g);


}

