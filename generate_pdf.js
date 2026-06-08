const fs = require('fs');
const { mdToPdf } = require('md-to-pdf');

(async () => {
    try {
        const pdf = await mdToPdf({ path: 'MANUAL_OPERACAO_DOCKSTATION.md' }, { 
            dest: 'MANUAL_OPERACAO_DOCKSTATION.pdf',
            pdf_options: {
                displayHeaderFooter: true,
                headerTemplate: '<span></span>',
                footerTemplate: '<div style="font-size: 10px; width: 100%; text-align: center; color: #555; padding-bottom: 5px;">Página <span class="pageNumber"></span> de <span class="totalPages"></span></div>',
                margin: { top: '30px', bottom: '50px', left: '30px', right: '30px' }
            }
        });
        if (pdf) fs.writeFileSync('MANUAL_OPERACAO_DOCKSTATION.pdf', pdf.content);
        console.log('PDF created successfully!');
    } catch (err) {
        console.error('Error creating PDF:', err);
    }
})();
