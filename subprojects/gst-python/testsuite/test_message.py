import unittest
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib, GObject

Gst.init(None)


class NewTest(unittest.TestCase):
    def testEOS(self):
        Gst.debug("creating new bin")
        b = Gst.Bin.new(None)
        Gst.debug("creating new EOS message from that bin")
        m = Gst.Message.new_eos(b)
        Gst.debug(f"got message : {m}")

    def message_application_cb(self, bus, message):
        Gst.debug("got application message")
        self.got_message = True
        self.loop.quit()

    def testApplication(self):
        self.loop = GLib.MainLoop()
        Gst.debug("creating new pipeline")
        bin = Gst.Pipeline.new(None)
        bus = bin.get_bus()
        bus.add_signal_watch()
        self.got_message = False
        bus.connect("message::application", self.message_application_cb)

        struc = Gst.Structure.new_empty("foo")
        msg = Gst.Message.new_application(bin, struc)
        # the bus is flushing in NULL, so we need to set the pipeline to READY
        bin.set_state(Gst.State.READY)
        bus.post(msg)
        self.loop.run()
        bus.remove_signal_watch()
        bin.set_state(Gst.State.NULL)
        assert self.got_message


class TestCreateMessages(unittest.TestCase):

    def setUp(self):
        self.element = Gst.Bin.new(None)

    def tearDown(self):
        del self.element

    def testCustomMessage(self):
        # create two custom messages using the same structure
        s = Gst.Structure.new_empty("something")
        assert s is not None
        e1 = Gst.Message.new_custom(Gst.MessageType.APPLICATION, self.element, s)
        assert e1 is not None
        e2 = Gst.Message.new_custom(Gst.MessageType.APPLICATION, self.element, s)
        assert e2 is not None

        # make sure the two structures are equal
        assert e1.get_structure().to_string() == e2.get_structure().to_string()

    def testTagMessage(self):
        # Create a taglist
        t = Gst.TagList.new_empty()

        # Add tags using the correct API
        t.add_value(Gst.TagMergeMode.APPEND, "title", "My Title")
        t.add_value(Gst.TagMergeMode.APPEND, "artist", "My Artist")

        # Create two messages using that same taglist
        m1 = Gst.Message.new_tag(self.element, t)
        assert m1 is not None
        m2 = Gst.Message.new_tag(self.element, t)
        assert m2 is not None

        # make sure the two messages have the same taglist
        t1 = m1.parse_tag()
        assert t1 is not None
        keys = t1.to_string().split(",")
        keys = [key.split("=")[0].strip() for key in keys if "=" in key]
        assert set(keys) == set(["artist", "title"])
        assert t1.get_string("title")[1] == "My Title"
        assert t1.get_string("artist")[1] == "My Artist"

        t2 = m2.parse_tag()
        assert t2 is not None
        keys = t2.to_string().split(",")
        keys = [key.split("=")[0].strip() for key in keys if "=" in key]
        assert set(keys) == set(["artist", "title"])
        assert t2.get_string("title")[1] == "My Title"
        assert t2.get_string("artist")[1] == "My Artist"

    def testTagFullMessage(self):
        # Create a taglist
        t = Gst.TagList.new_empty()
        t.add_value(Gst.TagMergeMode.APPEND, "album", "MyAlbum")
        t.add_value(
            Gst.TagMergeMode.APPEND, "track-number", GObject.Value(GObject.TYPE_UINT, 3)
        )

        # Create two messages using that same taglist
        m1 = Gst.Message.new_tag(self.element, t)
        assert m1 is not None
        m2 = Gst.Message.new_tag(self.element, t)
        assert m2 is not None

        # make sure the two messages have the same taglist
        t1 = m1.parse_tag()
        assert t1 is not None
        keys = t1.to_string().split(",")
        keys = [key.split("=")[0].strip() for key in keys if "=" in key]
        assert set(keys) == set(["album", "track-number"])
        assert t1.get_string("album")[1] == "MyAlbum"
        assert t1.get_uint("track-number")[1] == 3
        t2 = m2.parse_tag()
        assert t2 is not None
        keys = t2.to_string().split(",")
        keys = [key.split("=")[0].strip() for key in keys if "=" in key]
        assert set(keys) == set(["album", "track-number"])
        assert t2.get_string("album")[1] == "MyAlbum"
        assert t2.get_uint("track-number")[1] == 3

    def testStepStartMessage(self):
        m = Gst.Message.new_step_start(
            self.element, True, Gst.Format.TIME, 42, 1.0, True, True
        )
        assert m.type == Gst.MessageType.STEP_START
        active, format, amount, rate, flush, intermediate = m.parse_step_start()
        assert active is True
        assert format == Gst.Format.TIME
        assert amount == 42
        assert rate == 1.0
        assert flush == True
        assert intermediate == True

    def testStepDoneMessage(self):
        m = Gst.Message.new_step_done(
            self.element, Gst.Format.TIME, 42, 1.0, True, True, 54, True
        )
        assert m.type == Gst.MessageType.STEP_DONE

        fmt, am, rat, flu, inter, dur, eos = m.parse_step_done()
        assert fmt == Gst.Format.TIME
        assert am == 42
        assert rat == 1.0
        assert flu == True
        assert inter == True
        assert dur == 54
        assert eos == True

    def testStructureChangeMessage(self):
        p = Gst.Pad.new("blah", Gst.PadDirection.SINK)
        m = Gst.Message.new_structure_change(
            p, Gst.StructureChangeType.LINK, self.element, True
        )

        assert m.type == Gst.MessageType.STRUCTURE_CHANGE
        sct, owner, busy = m.parse_structure_change()
        assert sct == Gst.StructureChangeType.LINK
        assert owner == self.element
        assert busy == True

    def testRequestStateMessage(self):
        m = Gst.Message.new_request_state(self.element, Gst.State.NULL)
        assert m.type == Gst.MessageType.REQUEST_STATE
        assert m.parse_request_state() == Gst.State.NULL

    def testBufferingStatsMessage(self):
        Gst.debug("Creating buffering message")
        m = Gst.Message.new_buffering(self.element, 50)
        Gst.debug("Setting stats")
        m.set_buffering_stats(Gst.BufferingMode.LIVE, 30, 1024, 123456)
        assert m.type == Gst.MessageType.BUFFERING
        mode, ain, aout, left = m.parse_buffering_stats()
        assert mode == Gst.BufferingMode.LIVE
        assert ain == 30
        assert aout == 1024
        assert left == 123456


if __name__ == "__main__":
    unittest.main()
