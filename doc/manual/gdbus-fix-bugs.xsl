<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

	<!--  This fixes broken gdbus-codegen output -->

	<!--  Remove nested para elements -->
	<xsl:template match="para[para]">
		<xsl:apply-templates select="para"/>
	</xsl:template>

	<!--  Remove empty variablelist elements  -->
	<xsl:template match="variablelist[not(*)]">
	</xsl:template>

	<xsl:template match="node() | @*">
		<xsl:copy>
			<xsl:apply-templates select="node() | @*"/>
		</xsl:copy>
	</xsl:template>

</xsl:stylesheet>
