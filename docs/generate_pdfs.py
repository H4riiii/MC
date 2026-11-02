#!/usr/bin/env python3
"""
Convert all Markdown (.md) files in docs/ (except AGENT_LOG.md) to clean,
professional PDF documents.
Can be run anytime .md files change to regenerate matching PDFs.
"""

import os
import re
import sys
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Preformatted, HRFlowable
from reportlab.lib import colors

DOCS_DIR = os.path.dirname(os.path.abspath(__file__))

def sanitize_text(text):
    text = text.replace("&", "&amp;")
    text = text.replace("<", "&lt;")
    text = text.replace(">", "&gt;")
    # Convert inline code `foo`
    text = re.sub(r'`([^`]+)`', r'<font face="Courier" color="#b30000"><b>\1</b></font>', text)
    # Convert bold **text**
    text = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', text)
    # Convert italic *text* (word boundary or non-space)
    text = re.sub(r'(?<!\*)\*([A-Za-z0-9_][^*]*?[A-Za-z0-9_])\*(?!\*)', r'<i>\1</i>', text)
    return text

def parse_markdown_to_elements(md_path, styles):
    with open(md_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    elements = []
    in_code_block = False
    code_lines = []
    in_table = False
    table_rows = []

    for line in lines:
        raw_line = line.rstrip('\r\n')

        # Code block boundary
        if raw_line.strip().startswith('```'):
            if in_code_block:
                in_code_block = False
                code_text = "\n".join(code_lines)
                code_lines = []
                p_code = Preformatted(code_text, styles['CodeStyle'])
                t = Table([[p_code]], colWidths=[500])
                t.setStyle(TableStyle([
                    ('BACKGROUND', (0,0), (-1,-1), colors.HexColor('#f5f5f5')),
                    ('BOX', (0,0), (-1,-1), 1, colors.HexColor('#d0d0d0')),
                    ('TOPPADDING', (0,0), (-1,-1), 6),
                    ('BOTTOMPADDING', (0,0), (-1,-1), 6),
                    ('LEFTPADDING', (0,0), (-1,-1), 8),
                    ('RIGHTPADDING', (0,0), (-1,-1), 8),
                ]))
                elements.append(t)
                elements.append(Spacer(1, 8))
            else:
                in_code_block = True
                code_lines = []
            continue

        if in_code_block:
            code_lines.append(raw_line)
            continue

        # Tables (e.g. | col | col |)
        if raw_line.strip().startswith('|') and '|' in raw_line.strip()[1:]:
            # Check if separator row like | :--- | :--- |
            if re.match(r'^\s*\|(\s*:?-+:?\s*\|)+\s*$', raw_line):
                continue
            cells = [c.strip() for c in raw_line.strip().strip('|').split('|')]
            table_rows.append(cells)
            in_table = True
            continue
        else:
            if in_table:
                in_table = False
                if table_rows:
                    num_cols = max(len(r) for r in table_rows)
                    # Normalize row length
                    formatted_table = []
                    for i, r in enumerate(table_rows):
                        while len(r) < num_cols:
                            r.append('')
                        row_paras = []
                        for cell in r:
                            clean_cell = sanitize_text(cell)
                            p_style = styles['TableHeader'] if i == 0 else styles['TableCell']
                            row_paras.append(Paragraph(clean_cell, p_style))
                        formatted_table.append(row_paras)

                    t_width = 500 / num_cols
                    tab = Table(formatted_table, colWidths=[t_width] * num_cols)
                    tab.setStyle(TableStyle([
                        ('BACKGROUND', (0,0), (-1,0), colors.HexColor('#2c3e50')),
                        ('TEXTCOLOR', (0,0), (-1,0), colors.white),
                        ('ALIGN', (0,0), (-1,-1), 'LEFT'),
                        ('VALIGN', (0,0), (-1,-1), 'MIDDLE'),
                        ('GRID', (0,0), (-1,-1), 0.5, colors.HexColor('#bdc3c7')),
                        ('ROWBACKGROUNDS', (0,1), (-1,-1), [colors.HexColor('#ffffff'), colors.HexColor('#f8f9fa')]),
                        ('TOPPADDING', (0,0), (-1,-1), 4),
                        ('BOTTOMPADDING', (0,0), (-1,-1), 4),
                    ]))
                    elements.append(tab)
                    elements.append(Spacer(1, 8))
                table_rows = []

        # Empty lines
        if not raw_line.strip():
            elements.append(Spacer(1, 4))
            continue

        # Horizontal rules
        if re.match(r'^\s*[-*_]{3,}\s*$', raw_line):
            elements.append(HRFlowable(width="100%", thickness=1, color=colors.HexColor('#d0d0d0'), spaceBefore=6, spaceAfter=8))
            continue

        # Headings
        if raw_line.startswith('# '):
            h_text = sanitize_text(raw_line[2:])
            elements.append(Paragraph(h_text, styles['Heading1']))
            elements.append(Spacer(1, 6))
        elif raw_line.startswith('## '):
            h_text = sanitize_text(raw_line[3:])
            elements.append(Paragraph(h_text, styles['Heading2']))
            elements.append(Spacer(1, 4))
        elif raw_line.startswith('### '):
            h_text = sanitize_text(raw_line[4:])
            elements.append(Paragraph(h_text, styles['Heading3']))
            elements.append(Spacer(1, 4))
        elif raw_line.startswith('#### '):
            h_text = sanitize_text(raw_line[5:])
            elements.append(Paragraph(h_text, styles['Heading4']))
            elements.append(Spacer(1, 2))
        elif raw_line.startswith('> '):
            # Blockquote / Alert
            b_text = sanitize_text(raw_line[2:])
            elements.append(Paragraph(b_text, styles['Blockquote']))
            elements.append(Spacer(1, 4))
        elif raw_line.strip().startswith(('* ', '- ')):
            bullet_text = sanitize_text(raw_line.strip()[2:])
            elements.append(Paragraph(f"&bull;  {bullet_text}", styles['BulletItem']))
        elif re.match(r'^\s*\d+\.\s+', raw_line):
            num_text = re.sub(r'^\s*(\d+\.)\s+', r'<b>\1</b> ', raw_line)
            clean_num = sanitize_text(num_text)
            elements.append(Paragraph(clean_num, styles['BodyStyle']))
        else:
            p_text = sanitize_text(raw_line)
            elements.append(Paragraph(p_text, styles['BodyStyle']))

    return elements

def convert_md_to_pdf(md_path, pdf_path):
    doc = SimpleDocTemplate(
        pdf_path,
        pagesize=letter,
        rightMargin=45, leftMargin=45,
        topMargin=45, bottomMargin=45
    )

    base_styles = getSampleStyleSheet()
    styles = {
        'Heading1': ParagraphStyle('H1', parent=base_styles['Heading1'], fontName='Helvetica-Bold', fontSize=18, leading=22, textColor=colors.HexColor('#1a365d')),
        'Heading2': ParagraphStyle('H2', parent=base_styles['Heading2'], fontName='Helvetica-Bold', fontSize=14, leading=18, textColor=colors.HexColor('#2b6cb0')),
        'Heading3': ParagraphStyle('H3', parent=base_styles['Heading3'], fontName='Helvetica-Bold', fontSize=11, leading=15, textColor=colors.HexColor('#2d3748')),
        'Heading4': ParagraphStyle('H4', parent=base_styles['Heading4'], fontName='Helvetica-Bold', fontSize=10, leading=14, textColor=colors.HexColor('#4a5568')),
        'BodyStyle': ParagraphStyle('Body', parent=base_styles['Normal'], fontName='Helvetica', fontSize=9, leading=13, textColor=colors.HexColor('#2d3748'), spaceAfter=4),
        'BulletItem': ParagraphStyle('Bullet', parent=base_styles['Normal'], fontName='Helvetica', fontSize=9, leading=13, leftIndent=12, textColor=colors.HexColor('#2d3748'), spaceAfter=2),
        'Blockquote': ParagraphStyle('Quote', parent=base_styles['Normal'], fontName='Helvetica-Oblique', fontSize=9, leading=13, leftIndent=14, textColor=colors.HexColor('#4a5568'), backColor=colors.HexColor('#edf2f7')),
        'CodeStyle': ParagraphStyle('Code', fontName='Courier', fontSize=7.5, leading=10, textColor=colors.HexColor('#222222')),
        'TableHeader': ParagraphStyle('TH', fontName='Helvetica-Bold', fontSize=8, leading=10, textColor=colors.white, alignment=0),
        'TableCell': ParagraphStyle('TD', fontName='Helvetica', fontSize=7.5, leading=10, textColor=colors.HexColor('#2d3748'), alignment=0),
    }

    elements = parse_markdown_to_elements(md_path, styles)
    doc.build(elements)
    print(f"[OK] Generated: {os.path.basename(pdf_path)}")

def main():
    docs_files = sorted(os.listdir(DOCS_DIR))
    md_files = [f for f in docs_files if f.endswith('.md') and f.lower() != 'agent_log.md']

    print(f"Converting {len(md_files)} markdown files in {DOCS_DIR} to PDF...")
    for md_file in md_files:
        md_full = os.path.join(DOCS_DIR, md_file)
        pdf_name = os.path.splitext(md_file)[0] + '.pdf'
        pdf_full = os.path.join(DOCS_DIR, pdf_name)
        try:
            convert_md_to_pdf(md_full, pdf_full)
        except Exception as e:
            print(f"[ERROR] Failed to convert {md_file}: {e}")

if __name__ == '__main__':
    main()
