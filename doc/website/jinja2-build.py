#!/usr/bin/env python

import fnmatch
import os
import shutil
import sys

try:
	import jinja2
except ImportError:
	print >> sys.stderr, "python-jinja2 must be installed in order to build the website"

SRCDIR = os.path.abspath(os.environ.get("SRCDIR", "."))
INDIR = os.path.join(SRCDIR, "doc/website/content")
BUILDDIR = os.path.abspath(os.environ.get("BUILDDIR", SRCDIR))
OUTDIR = os.path.join(BUILDDIR, "html")

jinja_env = None

def main(args):
	global jinja_env

	args = {
		"version": open(BUILDDIR + "/doc/version.xml").read().strip()
	}

	os.chdir(SRCDIR)

	loader = jinja2.FileSystemLoader(INDIR)
	jinja_env = jinja2.Environment(loader=loader, autoescape=True,
	                               undefined=jinja2.StrictUndefined)

	os.chdir(INDIR)
	os.path.walk(".", process_file, args)
	print >> sys.stderr, "Results: file://%s/index.html" % OUTDIR

def process_file(args, dirname, names):
	directory = os.path.normpath(os.path.join(OUTDIR, dirname))
	if not os.path.exists(directory):
		os.mkdir(directory)

	for name in names:
		path = os.path.join(dirname, name)
		if os.path.isdir(path):
			continue

		if fnmatch.fnmatch(path, "base*.html"):
			continue

		elif fnmatch.fnmatch(path, "*.html"):
			print >> sys.stderr, name
			output = os.path.join(OUTDIR, path)
			template = jinja_env.get_template(path)
			data = unicode(template.render(**args)).encode("utf-8")
			if os.path.exists(output):
				os.unlink(output)
			with open(output, 'w') as f:
				f.write(data)
			os.chmod(output, 0444)

		else:
			output = os.path.join(OUTDIR, path)
			if os.path.exists(output):
				os.unlink(output)
			shutil.copy(path, output)
			os.chmod(output, 0444)


# For running as a standalone server
if __name__ == "__main__":
	sys.exit(main(sys.argv))
