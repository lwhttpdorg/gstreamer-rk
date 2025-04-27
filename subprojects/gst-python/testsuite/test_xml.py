from gi.repository import Gst
import unittest


class PadTest(unittest.TestCase):

    def setUp(self):
        Gst.init(None)

    def testQuery(self):
        # don't run this test if we don't have the libxml2 module
        import xml.etree.ElementTree as ET

        import textwrap

        xml_data = textwrap.dedent(
            """\
            <?xml version="1.0"?>
            <gstreamer xmlns:gst="http://gstreamer.net/gst-core/1.0/">
              <gst:element>
                <gst:name>test-pipeline</gst:name>
                <gst:type>pipeline</gst:type>
                <gst:param>
                  <gst:name>name</gst:name>
                  <gst:value>test-pipeline</gst:value>
                </gst:param>
              </gst:element>
            </gstreamer>"""
        )

        root = ET.fromstring(xml_data)
        element_node = root.find(".//{http://gstreamer.net/gst-core/1.0/}element")

        # Get the element type (pipeline in this case)
        element_type = element_node.find(
            "{http://gstreamer.net/gst-core/1.0/}type"
        ).text

        # Get the element name from the XML
        element_name = element_node.find(
            "{http://gstreamer.net/gst-core/1.0/}name"
        ).text

        # Create a simple pipeline string without hardcoding the name
        pipeline_str = f"{element_type}"

        pipeline = Gst.parse_launch(pipeline_str)

        # Set the name of the pipeline after creation
        pipeline.set_name(element_name)

        assert pipeline is not None
        assert isinstance(pipeline, Gst.Pipeline)
        assert pipeline.get_name() == "test-pipeline"


if __name__ == "__main__":
    unittest.main()
