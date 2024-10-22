import unittest
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst


class RegistryTest(unittest.TestCase):
    def setUp(self):
        Gst.init(None)
        self.registry = Gst.Registry.get()
        self.plugins = self.registry.get_plugin_list()

    def test_get_default(self):
        assert self.registry is not None

    def test_plugin_list(self):
        names = [p.get_name() for p in self.plugins]
        assert "coreelements" in names, "coreelements plugin not found"


class RegistryFeatureTest(unittest.TestCase):
    def setUp(self):
        Gst.init(None)
        self.registry = Gst.Registry.get()
        self.plugins = self.registry.get_plugin_list()
        self.efeatures = self.registry.get_feature_list(Gst.ElementFactory)
        self.tfeatures = self.registry.get_feature_list(Gst.TypeFindFactory)
        self.ifeatures = self.registry.get_feature_list(Gst.DeviceProviderFactory)

    def test_feature_list(self):
        with self.assertRaises(TypeError):
            self.registry.get_feature_list("invalid_type")

        elements = [f.get_name() for f in self.efeatures]
        assert "fakesink" in elements, "fakesink element not found"

        typefinds = [f.get_name() for f in self.tfeatures]
        assert len(typefinds) > 0, "No TypeFindFactory features found"

        device_providers = [f.get_name() for f in self.ifeatures]
        assert len(device_providers) > 0, "No DeviceProviderFactory features found"


if __name__ == "__main__":
    unittest.main()
