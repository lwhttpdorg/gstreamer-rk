import sys
import gi
import logging

gi.require_version("Gst", "1.0")

from gi.repository import Gst


# CustomData structure to hold pipeline elements
# This will make it a lot easier to pass data to the various
# necessary callback functions we need to use with gstreamer


class CustomData:
    def __init__(self):
        self.pipeline = None
        self.source = None
        self.convert = None
        self.resample = None
        self.sink = None
        self.vconvert = None
        self.vsink = None


# callback function

# This guy will "handle" the adding of a new pad called by
# `data.source.connect("pad-added", pad_added_handler, data)` later.

# This is triggered when it is connected to an element and a specific signal.
# In this case, we connect (later in the script, keep reading) it like so:
# `data.source.connect("pad-added", pad_added_handler, data)`
# where the "pad-added" is the GSignal that will trigger this callback,
# the second arg is the callback,
# and the third arg is data to pass to the callback `pad_added_handler`

gi.require_version("GLib", "2.0")
gi.require_version("GObject", "2.0")

# the callback itself:
# src is the GstElement which triggered the signal
# in this case it's the uridecodebin because it's the only attached.
# new_pad is the GstPad that has just ben added to the src element.
# this newly added pad is usually the pad we want to link.
# data is the pointer/class we provided earlier.


def pad_added_handler(src, new_pad, data):
    print("Entered pad_added_handler!")
    sink_pad = data.convert.get_static_pad("sink")
    vsink_pad = data.vconvert.get_static_pad("sink")
    print(f"vsink pad: {vsink_pad}")

    # This block attempts to keep pads from linking to a new one once linked,
    # but there is a convert pad already linked to a resample pad, so
    # sink_pad.is_linked currently always returns True which keeps
    # this from working.
    # if sink_pad.is_linked:
    # print("already linked, ignoring")
    # return

    # Here we make sure we are only dealing with raw audio.
    new_pad_caps = new_pad.get_current_caps()
    new_pad_struct = new_pad_caps.get_structure(0)
    new_pad_type = new_pad_struct.get_name()
    print(f"New pad caps: {new_pad_caps}")
    print(f"New pad struct: {new_pad_struct}")
    print(f"New pad type: {new_pad_type}")
    if not new_pad_type.startswith("audio/x-raw"):
        print(
            f"It has a type of {new_pad_type} which is not raw audio, attempting to link video pad."
        )
        if not new_pad_type.startswith("video/x-raw"):
            print(f"It has a type of {new_pad_type} which is not raw video, ignoring")
            return
        else:
            print("Linking video pad")

            ret = new_pad.link(vsink_pad)
            print(f"ret: {ret}")
            if ret is not None or ret:
                print(f"{ret}")
                print(f"Type is {new_pad_type} but link failed")

            else:
                print(f"Link succeded (type {new_pad_type}")
                return
        return

    ret = new_pad.link(sink_pad)
    if ret is not None or ret:
        print(f"Type is {new_pad_type} but link failed")
    else:
        print(f"Link succeeded (type {new_pad_type}")
    # vsink_pad.unref()
    # sink_pad.unref()
    # new_pad.unref()


logging.basicConfig(
    level=logging.DEBUG, format="[%(name)s] [%(levelname)8s] - %(message)s"
)
logger = logging.getLogger(__name__)

# Instantiate the custom data class for passing values around
data = CustomData()
# Initialize GStreamer
Gst.init(sys.argv[1:])

# Create the elements for use in the pipeline
data.source = Gst.ElementFactory.make("uridecodebin", "source")
data.convert = Gst.ElementFactory.make("audioconvert", "convert")
data.resample = Gst.ElementFactory.make("audioresample", "resample")
data.sink = Gst.ElementFactory.make("autoaudiosink", "sink")
data.vconvert = Gst.ElementFactory.make("videoconvert", "vconvert")
data.vsink = Gst.ElementFactory.make("autovideosink", "vsink")
# uridecodebin will instantiate all necessary elements(sources, demuxers, and decoders)
# to turn a URI into raw audio and/or video streams. It does half the work that playbin does.
# SINCE uridecodebin CONTAINS DEMUXERS, ITS SOURCE PADS ARE NOT INITIALLY AVAILABLE
# and we will need to LINK THEM ON THE FLY.

# audioconvert is handy for converting between different audio formats, making sure this example
# will work on any platform, since the format produced by the audio decoder
# might not be the same that the audio sink expects.

# audioresample is useful for converting between different audio sample rates,
# similarly making sure that this example will work on any platform,
# since the audio sample rate produced by the audio decoder might not
# be one that the audio sink supports.

# autoaudiosink is the equivalent of autovideosink for audio.

# create pipeline
data.pipeline = Gst.Pipeline.new("test-pipeline")

# check if elements were created
if (
    not data.pipeline
    or not data.source
    or not data.convert
    or not data.resample
    or not data.sink
    or not data.vconvert
    or not data.vsink
):
    logger.error("Not all elements could be created.")
    sys.exit(1)

# build the pipeline, but without adding the source at this point.

data.pipeline.add(data.source)
data.pipeline.add(data.convert)
data.pipeline.add(data.resample)
data.pipeline.add(data.sink)
data.pipeline.add(data.vconvert)
data.pipeline.add(data.vsink)

# link elements but NOT WITH SOURCE because it doesn't have any source pads yet.


if not data.convert.link(data.resample):
    logger.error("error linking convert and resample")
    sys.exit(1)

if not data.resample.link(data.sink):
    logger.error("error linking resample and sink")
    sys.exit(1)

if not data.vconvert.link(data.vsink):
    logger.error("error linking vconvert and vsink")
    sys.exit(1)

# Set URI to play
data.source.props.uri = (
    "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm"
)

# Connect to the pad-added signal
### SIGNALS
data.source.connect("pad-added", pad_added_handler, data)

# GSignals are a crucial point in GStreamer. They allow you to be notified
# by means of a callback when something interesting has happened.
# Signals are identified by a name, and each GObject has its own signals

# In this line we are *attaching* the "pad-added" signal of our source
# (an uridecodebin element). To do so, we use data.GObject.connect()
# and provide the callback function to be used (pad_added_handler)
# and the "a data pointer" (in this case in python we just give it
# the data class that we instantiated earlier)
# GStreamer itself doesn't do anything with the "data pointer", it only
# forwards it to the callback function so we can share information with it.
# In this case, passed a pointer to the CustomData structure built specifically
# for this purpose

# The signals that a GstElement generates can be found in its documentation
# or using the gst-inspect-1.0 tool as described in Basic tutorial 10: GStreamer tools

# WHEN SOURCE ELEMENT has enough information to start producing data,
# it will create source pads, and trigger the "pad-added" signal.
# at this point, the callback `pad_added_handler` will be called.

# Start playing
ret = data.pipeline.set_state(Gst.State.PLAYING)
###GStreamer STATES
# They are NULL <-> READY <-> PAUSED <-> PLAYING
# You can only move adjacently, e.g. to go from NULL to PLAYING you
# must go NULL -> READY -> PAUSED -> PLAYING and vice-versa.
# However, if you COMMAND GStreamer to go from
# NULL to PLAYING or any other non-adjacent move,
# GStreamer will do the intermediate transitions for you.
if ret == Gst.StateChangeReturn.FAILURE:
    print("Unable to set the pipeline to the playing state.")
    data.pipeline.unref()
    sys.exit(1)

# Listen to the bus

bus = data.pipeline.get_bus()
while True:
    msg = bus.timed_pop_filtered(
        Gst.CLOCK_TIME_NONE,
        Gst.MessageType.STATE_CHANGED | Gst.MessageType.ERROR | Gst.MessageType.EOS,
    )

    if msg:
        err = None
        debug_info = None
        if msg.type == Gst.MessageType.ERROR:
            err, debug_info = msg.parse_error()
            print(f"Error received from element {msg.src.name}: {err.message}")
            print(f"Debugging information: {debug_info if debug_info else 'none'}")
            break
        elif msg.type == Gst.MessageType.EOS:
            print("End of stream reached.")
            break
        elif msg.type == Gst.MessageType.STATE_CHANGED:
            if msg.src == data.pipeline:
                old_state, new_state, pending_state = msg.parse_state_changed()
                print(f"Pipeline old state: {old_state} \nNew state: {new_state}")
