"""Small WordprocessingML (ISO 29500 *strict*) writer used by build_report.py.

The IEEE conference template (template/conference-template-a4.docx) is a strict-OOXML
package, so everything written here uses the strict namespaces and the template's own
paragraph styles (papertitle, Author, Abstract, Keywords, Heading1-5, BodyText, bulletlist,
equation, figurecaption, tablehead, tablecolhead, tablecopy, references).
"""
import re
from xml.sax.saxutils import escape

EMU_PER_PT = 12700
REL_IMAGE = "http://purl.oclc.org/ooxml/officeDocument/relationships/image"
PIC_URI = "http://purl.oclc.org/ooxml/drawingml/picture"


def t(text):
    return f'<w:t xml:space="preserve">{escape(text)}</w:t>'


def run(text, b=False, i=False, sup=False, sub=False, color=None, font=None, size=None, caps=False):
    pr = ""
    if font:
        pr += f'<w:rFonts w:ascii="{font}" w:hAnsi="{font}" w:cs="{font}"/>'
    if b:
        pr += "<w:b/><w:bCs/>"
    if i:
        pr += "<w:i/><w:iCs/>"
    if caps:
        pr += "<w:smallCaps/>"
    if color:
        pr += f'<w:color w:val="{color}"/>'
    if size:
        pr += f'<w:sz w:val="{size}"/><w:szCs w:val="{size}"/>'
    if sup:
        pr += '<w:vertAlign w:val="superscript"/>'
    if sub:
        pr += '<w:vertAlign w:val="subscript"/>'
    rpr = f"<w:rPr>{pr}</w:rPr>" if pr else ""
    return f"<w:r>{rpr}{t(text)}</w:r>"


def tab():
    return "<w:r><w:tab/></w:r>"


def brk():
    return "<w:r><w:br/></w:r>"


def para(style, content, jc=None, keep_next=False, extra_ppr=""):
    ppr = f'<w:pStyle w:val="{style}"/>' if style else ""
    if keep_next:
        ppr += "<w:keepNext/>"
    ppr += extra_ppr
    if jc:
        ppr += f'<w:jc w:val="{jc}"/>'
    return f"<w:p><w:pPr>{ppr}</w:pPr>{content}</w:p>"


# ------------------------------------------------------------------ inline markup
# Text strings use a tiny markup:  *italic*  **bold**  ^{sup}  _{sub}  [[TBD:...]]  [[REF:n]] (citation link)
TOKEN = re.compile(r"(\[\[REF:\d+\]\]|\*\*.+?\*\*|\*.+?\*|\^\{.+?\}|_\{.+?\}|\[\[TBD:.*?\]\]|`.+?`)")


def rich(text, base_i=False, base_b=False):
    out = []
    for part in TOKEN.split(text):
        if not part:
            continue
        if part.startswith("[[REF:"):
            # citation [n]: internal link to the bookmark on reference n (plain text look, as in IEEE papers)
            n = part[6:-2]
            out.append(f'<w:hyperlink w:anchor="_RefCite{n}" w:history="1">'
                       + run(f"[{n}]", i=base_i, b=base_b) + "</w:hyperlink>")
        elif part.startswith("**"):
            out.append(run(part[2:-2], b=True, i=base_i))
        elif part.startswith("*"):
            out.append(run(part[1:-1], i=not base_i, b=base_b))
        elif part.startswith("^{"):
            out.append(run(part[2:-1], sup=True, i=base_i, b=base_b))
        elif part.startswith("_{"):
            out.append(run(part[2:-1], sub=True, i=base_i, b=base_b))
        elif part.startswith("[[TBD:"):
            out.append(run("[" + part[6:-2] + "]", b=True, color="C00000", i=base_i))
        elif part.startswith("`"):
            out.append(run(part[1:-1], font="Courier New", size=16))
        else:
            out.append(run(part, i=base_i, b=base_b))
    return "".join(out)


# ------------------------------------------------------------------ images
class Images:
    def __init__(self):
        self.items = []          # (rid, target, data_path)
        self.next_id = 100

    def add(self, path):
        rid = f"rIdImg{len(self.items) + 1}"
        ext = "jpeg" if path.lower().endswith((".jpg", ".jpeg")) else "png"
        target = f"media/image{len(self.items) + 1}.{ext}"
        self.items.append((rid, target, path))
        return rid


def image_run(images, path, width_pt=None, dpi=600):
    """Inline picture run. Without width_pt the image keeps its true size at `dpi`."""
    from PIL import Image
    w, h = Image.open(path).size
    if width_pt is None:
        width_pt = w / dpi * 72.0
    cx = int(width_pt * EMU_PER_PT)
    cy = int(cx * h / w)
    rid = images.add(path)
    images.next_id += 1
    n = images.next_id
    return (
        f'<w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0">'
        f'<wp:extent cx="{cx}" cy="{cy}"/><wp:docPr id="{n}" name="Equation {n}"/>'
        f'<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect="1"/></wp:cNvGraphicFramePr>'
        f'<a:graphic><a:graphicData uri="{PIC_URI}"><pic:pic xmlns:pic="{PIC_URI}">'
        f'<pic:nvPicPr><pic:cNvPr id="{n}" name="eq{n}.png"/><pic:cNvPicPr/></pic:nvPicPr>'
        f'<pic:blipFill><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>'
        f'<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        f'<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>'
        f'</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>')


def equation_img(images, png, number):
    """Numbered display equation: centred image, number at the right margin (template tabs)."""
    return (f'<w:p><w:pPr><w:pStyle w:val="equation"/></w:pPr>{tab()}{image_run(images, png)}'
            f'{tab()}{run(f"({number})", font="Times New Roman")}</w:p>')


def render_equation(tex, path, fontsize=9.5):
    """LaTeX-style math (matplotlib mathtext, STIX = Times-like) -> 600-dpi black PNG."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"mathtext.fontset": "stix", "font.family": "STIXGeneral"})
    fig = plt.figure(figsize=(0.01, 0.01))
    fig.text(0, 0, f"${tex}$", fontsize=fontsize, color="black")
    fig.savefig(path, dpi=600, bbox_inches="tight", pad_inches=0.02, transparent=False, facecolor="white")
    plt.close(fig)
    return path


def image_para(images, path, width_pt, style=None, jc="center"):
    from PIL import Image
    w, h = Image.open(path).size
    cx = int(width_pt * EMU_PER_PT)
    cy = int(cx * h / w)
    rid = images.add(path)
    images.next_id += 1
    n = images.next_id
    drawing = (
        f'<w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0">'
        f'<wp:extent cx="{cx}" cy="{cy}"/><wp:docPr id="{n}" name="Figure {n}"/>'
        f'<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect="1"/></wp:cNvGraphicFramePr>'
        f'<a:graphic><a:graphicData uri="{PIC_URI}"><pic:pic xmlns:pic="{PIC_URI}">'
        f'<pic:nvPicPr><pic:cNvPr id="{n}" name="image{n}.png"/><pic:cNvPicPr/></pic:nvPicPr>'
        f'<pic:blipFill><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>'
        f'<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        f'<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>'
        f'</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>')
    return f'<w:p><w:pPr><w:keepNext/><w:spacing w:before="4pt" w:after="0pt"/><w:jc w:val="{jc}"/></w:pPr>{drawing}</w:p>'


# ------------------------------------------------------------------ tables
def table(rows, widths_pt, header_rows=1, align=None):
    """rows: list of rows, each a list of cell strings (rich markup). '' for empty."""
    total = sum(widths_pt)
    grid = "".join(f'<w:gridCol w:w="{w}pt"/>' for w in widths_pt)
    border = ('<w:tblBorders><w:top w:val="single" w:sz="4" w:space="0" w:color="000000"/>'
              '<w:start w:val="single" w:sz="4" w:space="0" w:color="000000"/>'
              '<w:bottom w:val="single" w:sz="4" w:space="0" w:color="000000"/>'
              '<w:end w:val="single" w:sz="4" w:space="0" w:color="000000"/>'
              '<w:insideH w:val="single" w:sz="4" w:space="0" w:color="000000"/>'
              '<w:insideV w:val="single" w:sz="4" w:space="0" w:color="000000"/></w:tblBorders>')
    out = [f'<w:tbl><w:tblPr><w:tblW w:w="{total}pt" w:type="dxa"/><w:jc w:val="center"/>{border}'
           f'<w:tblLayout w:type="fixed"/><w:tblCellMar><w:start w:w="2.5pt" w:type="dxa"/>'
           f'<w:end w:w="2.5pt" w:type="dxa"/></w:tblCellMar></w:tblPr><w:tblGrid>{grid}</w:tblGrid>']
    for r, row in enumerate(rows):
        hdr = r < header_rows
        keep = "<w:keepNext/>" if r < len(rows) - 1 else ""     # keep the whole table on one page/column
        out.append("<w:tr><w:trPr><w:cantSplit/>" + ("<w:tblHeader/>" if hdr else "") + "</w:trPr>")
        for c, cell in enumerate(row):
            jc = "center" if (hdr or (align and align[c] == "c")) else ("end" if align and align[c] == "r" else "start")
            style = "tablecolhead" if hdr else "tablecopy"
            out.append(f'<w:tc><w:tcPr><w:tcW w:w="{widths_pt[c]}pt" w:type="dxa"/><w:vAlign w:val="center"/></w:tcPr>'
                       f'<w:p><w:pPr><w:pStyle w:val="{style}"/>{keep}<w:jc w:val="{jc}"/></w:pPr>{rich(cell)}</w:p></w:tc>')
        out.append("</w:tr>")
    out.append("</w:tbl>")
    return "".join(out)


# ------------------------------------------------------------------ equations (Office Math)
def m_r(text, italic=True):
    sty = "" if italic else '<m:rPr><m:sty m:val="p"/></m:rPr>'
    return f"<m:r>{sty}<m:t>{escape(text)}</m:t></m:r>"


def m_f(num, den):
    return f"<m:f><m:num>{num}</m:num><m:den>{den}</m:den></m:f>"


def m_sup(base, sup):
    return f"<m:sSup><m:e>{base}</m:e><m:sup>{sup}</m:sup></m:sSup>"


def m_sub(base, sub):
    return f"<m:sSub><m:e>{base}</m:e><m:sub>{sub}</m:sub></m:sSub>"


def m_paren(inner, l="(", r=")"):
    return f'<m:d><m:dPr><m:begChr m:val="{l}"/><m:endChr m:val="{r}"/></m:dPr><m:e>{inner}</m:e></m:d>'


def m_sum(lower, upper, body):
    return (f'<m:nary><m:naryPr><m:chr m:val="&#8721;"/><m:limLoc m:val="undOvr"/></m:naryPr>'
            f"<m:sub>{lower}</m:sub><m:sup>{upper}</m:sup><m:e>{body}</m:e></m:nary>")


def equation(omml, number):
    return (f'<w:p><w:pPr><w:pStyle w:val="equation"/></w:pPr>{tab()}<m:oMath>{omml}</m:oMath>'
            f'{tab()}{run(f"({number})", font="Times New Roman")}</w:p>')


# ------------------------------------------------------------------ sections
def sect(cols=2, space="18pt", first=False, top="54pt"):
    colxml = f'<w:cols w:num="{cols}" w:space="{space}"/>' if cols > 1 else '<w:cols w:space="36pt"/>'
    typ = "" if first else '<w:type w:val="continuous"/>'
    # first page uses the template footer (rId8), the other pages the page-number footer (rIdPgNum);
    # later continuous sections inherit both
    foot = ('<w:footerReference w:type="default" r:id="rIdPgNum"/>'
            '<w:footerReference w:type="first" r:id="rId8"/>') if first else ""
    tp = "<w:titlePg/>" if first else ""
    return (f'<w:sectPr>{foot}{typ}<w:pgSz w:w="595.30pt" w:h="841.90pt" w:code="9"/>'
            f'<w:pgMar w:top="{top}" w:right="45.35pt" w:bottom="72pt" w:left="45.35pt" w:header="36pt" '
            f'w:footer="36pt" w:gutter="0pt"/>{colxml}{tp}<w:docGrid w:linePitch="360"/></w:sectPr>')


def section_break(**kw):
    return f"<w:p><w:pPr><w:spacing w:before='0pt' w:after='0pt'/>{sect(**kw)}</w:pPr></w:p>".replace("'", '"')


def bookmark(name, bid, content):
    """Wrap run content in a bookmark (target of internal hyperlinks)."""
    return f'<w:bookmarkStart w:id="{bid}" w:name="{name}"/>{content}<w:bookmarkEnd w:id="{bid}"/>'


def page_number_paragraph():
    """Centred footer paragraph holding a PAGE field (8 pt, as the template footer)."""
    rp = '<w:rPr><w:sz w:val="16"/><w:szCs w:val="16"/></w:rPr>'
    return ('<w:p><w:pPr><w:pStyle w:val="Footer"/><w:jc w:val="center"/></w:pPr>'
            f'<w:r>{rp}<w:fldChar w:fldCharType="begin"/></w:r>'
            f'<w:r>{rp}<w:instrText xml:space="preserve"> PAGE </w:instrText></w:r>'
            f'<w:r>{rp}<w:fldChar w:fldCharType="separate"/></w:r>'
            f'<w:r>{rp}<w:t>1</w:t></w:r>'
            f'<w:r>{rp}<w:fldChar w:fldCharType="end"/></w:r></w:p>')
