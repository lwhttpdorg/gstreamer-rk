import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib

import unittest
import time
import sys

Gst.init(None)


class BusSignalTest(unittest.TestCase):
    def testGoodConstructor(self):
        loop = GLib.MainLoop()
        Gst.info("creating pipeline")
        pipeline = Gst.parse_launch("fakesrc ! fakesink")
        Gst.info("getting bus")
        bus = pipeline.get_bus()
        Gst.info("got bus")
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        assert bus.__grefcount__ == 2
        assert pipeline.__grefcount__ == 1
        Gst.info("about to add a watch on the bus")
        watch_id = bus.connect("message", self._message_received, pipeline, loop, "one")
        bus.add_signal_watch()
        Gst.info("added a watch on the bus")
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        assert bus.__grefcount__ == 3
        assert pipeline.__grefcount__ == 1

        Gst.info("setting to playing")
        ret = pipeline.set_state(Gst.State.PLAYING)
        Gst.info("set to playing %s, loop.run" % ret)
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        loop.run()

        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info("setting to paused")
        ret = pipeline.set_state(Gst.State.PAUSED)
        Gst.info("set to paused %s, loop.run" % ret)
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        loop.run()
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))

        Gst.info("setting to ready")
        ret = pipeline.set_state(Gst.State.READY)
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info("set to READY %s, loop.run" % ret)
        loop.run()

        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info("setting to NULL")
        ret = pipeline.set_state(Gst.State.NULL)
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info("set to NULL %s" % ret)
        assert bus.__grefcount__ == 3
        while pipeline.__grefcount__ > 1:
            Gst.debug("waiting for pipeline refcount to drop")
            time.sleep(0.1)
        assert pipeline.__grefcount__ == 1

        Gst.info("about to remove the watch id")
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        bus.remove_signal_watch()
        Gst.info("bus watch id removed")
        bus.disconnect(watch_id)
        Gst.info("disconnected callback")
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info(
            "pipeliner:%d/%d busr:%d"
            % (pipeline.__grefcount__, pipeline.__grefcount__, bus.__grefcount__)
        )

        assert bus.__grefcount__ == 2
        assert pipeline.__grefcount__ == 1

        Gst.info("removing pipeline")
        del pipeline
        Gst.info("pipeline removed")
        Gst.info("busr:%d" % bus.__grefcount__)

        # flush the bus
        bus.set_flushing(True)
        bus.set_flushing(False)
        assert bus.__grefcount__ == 1

    def _message_received(self, bus, message, pipeline, loop, id):
        assert isinstance(bus, Gst.Bus)
        assert isinstance(message, Gst.Message)
        assert id == "one"
        loop.quit()
        return True

    def testSyncHandlerCallbackRefcount(self):
        def callback1():
            pass

        def callback2():
            pass

        bus = Gst.Bus()

        # set
        assert sys.getrefcount(callback1) == 2
        bus.set_sync_handler(callback1)
        assert sys.getrefcount(callback1) == 3

        # set again
        assert sys.getrefcount(callback1) == 3
        bus.set_sync_handler(callback1)
        assert sys.getrefcount(callback1) == 3

        # replace this erros out in gst_bus_set_sync_handler, but we need to check that
        # we don't leak anyway
        assert sys.getrefcount(callback2) == 2
        bus.set_sync_handler(callback2)
        assert sys.getrefcount(callback1) == 2
        assert sys.getrefcount(callback2) == 3

        # unset
        bus.set_sync_handler(None)
        assert sys.getrefcount(callback2) == 2


class BusAddWatchTest(unittest.TestCase):
    def testADumbExample(self):
        pipeline = Gst.parse_launch("fakesrc ! fakesink")
        bus = pipeline.get_bus()

    def testGoodConstructor(self):
        loop = GLib.MainLoop()
        pipeline = Gst.parse_launch("fakesrc ! fakesink")
        bus = pipeline.get_bus()

        Gst.info("creating pipeline")
        Gst.info("getting bus")
        Gst.info("got bus")
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        assert bus.__grefcount__ == 2
        assert pipeline.__grefcount__ == 1
        Gst.info("about to add a watch on the bus")
        watch_id = bus.add_watch(
            GLib.PRIORITY_DEFAULT, self._message_received, pipeline, loop, "one"
        )
        Gst.info("added a watch on the bus")
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        assert bus.__grefcount__ == 3
        assert pipeline.__grefcount__ == 1

        Gst.info("setting to playing")
        ret = pipeline.set_state(Gst.State.PLAYING)
        Gst.info("set to playing %s, loop.run" % ret)
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        loop.run()

        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info("setting to paused")
        ret = pipeline.set_state(Gst.State.PAUSED)
        Gst.info("set to paused %s, loop.run" % ret)
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        loop.run()
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))

        Gst.info("setting to ready")
        ret = pipeline.set_state(Gst.State.READY)
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info("set to READY %s, loop.run" % ret)
        loop.run()

        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info("setting to NULL")
        ret = pipeline.set_state(Gst.State.NULL)
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info("set to NULL %s" % ret)
        assert bus.__grefcount__ == 3
        assert pipeline.__grefcount__ == 1

        Gst.info("about to remove the watch id")
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        assert GLib.source_remove(watch_id)
        Gst.info("bus watch id removed")
        Gst.info("pipeliner:%d busr:%d" % (pipeline.__grefcount__, bus.__grefcount__))
        Gst.info(
            "pipeliner:%d/%d busr:%d"
            % (pipeline.__grefcount__, pipeline.__grefcount__, bus.__grefcount__)
        )

        assert bus.__grefcount__ == 2
        assert pipeline.__grefcount__ == 1

        Gst.info("removing pipeline")
        del pipeline
        Gst.info("pipeline removed")
        Gst.info("busr:%d" % bus.__grefcount__)

        # flush the bus
        bus.set_flushing(True)
        bus.set_flushing(False)
        assert bus.__grefcount__ == 1

    def _message_received(self, bus, message, pipeline, loop, id):
        assert isinstance(bus, Gst.Bus)
        assert isinstance(message, Gst.Message)
        assert id == "one"
        # doesn't the following line stop the mainloop before the end of the state change ?
        loop.quit()
        return True


if __name__ == "__main__":
    unittest.main()
