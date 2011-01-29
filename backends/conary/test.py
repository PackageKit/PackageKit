#!/usr/bin/env python

import unittest
import os

import XMLCache

class TestXMLRepo(unittest.TestCase):
    def setUp(self):
        test_label = 'test-label'
        self.xml_repo = XMLCache.XMLRepo(label=test_label, path=os.getcwd(), pk=None)

    def test_search_name_count(self):
        self.assertEqual(1, len(self.xml_repo.search(['kernel'], 'name')))

    def test_search_name_count2(self):
        self.assertEqual(1, len(self.xml_repo.search(['kernel', 'kernel'], 'name')))

    def test_search_name_name(self):
        self.assertEqual('kernel',
                self.xml_repo.search(['kernel'], 'name')[0]['name'])

    def test_search_name_url(self):
        self.assertEqual('',
                self.xml_repo.search(['kernel'], 'name')[0]['url'])

    def test_search_detail_count(self):
        self.assertEqual(1, len(self.xml_repo.search(['xchat'], 'details')))

    def test_search_detail_version(self):
        self.assertEqual('2.8.8-1-1',
                self.xml_repo.search(['xchat'], 'details')[0]['version'])

    def test_search_group_count(self):
        self.assertEqual(1, len(self.xml_repo.search(['internet'], 'group')))

    def test_search_group_count2(self):
        self.assertEqual(1, len(self.xml_repo.search(['internet', 'internet'], 'group')))

if __name__ == '__main__':
    unittest.main()
